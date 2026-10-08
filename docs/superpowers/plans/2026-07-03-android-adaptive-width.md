# Android Adaptive Width Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cap the grouped Cupertino screens at an iOS readable width (640dp, centered) so tablets stop stretching rows edge-to-edge, and make the Library toolbar fit phone widths by moving Settings into the nav bar.

**Architecture:** One new `ReadableContent` composable in the cupertino kit wraps the whole screen column of the four grouped screens. Geometric no-op below 640dp, so phones need no branches. Library grid stays full-width; its Settings pill moves to `CupertinoNavBar`'s existing `trailing` slot.

**Tech Stack:** Existing Kotlin 2.0.21 + Compose foundation. No new dependencies.

## Global Constraints

- Build with `JAVA_HOME=$HOME/Android/jdk17`; gradle working dir `/home/sian/SameBoy/Android`.
- No Material/Material3. No new dependencies. No public-signature changes.
- Emulator screen untouched.
- No test frameworks; per-task verification = `JAVA_HOME=$HOME/Android/jdk17 ./gradlew :app:compileDebugJavaWithJavac` BUILD SUCCESSFUL; final verification on the OnePlus Pad (Task 2).

---

### Task 1: ReadableContent + apply to screens + Library nav action

**Files:**
- Modify: `Android/app/src/main/java/io/sameboy/android/cupertino/Components.kt` (add composable at end)
- Modify: `Android/app/src/main/java/io/sameboy/android/SettingsUi.kt:87`
- Modify: `Android/app/src/main/java/io/sameboy/android/LinkUi.kt:52`
- Modify: `Android/app/src/main/java/io/sameboy/android/RemapUi.kt:47`
- Modify: `Android/app/src/main/java/io/sameboy/android/PrinterUi.kt:50`
- Modify: `Android/app/src/main/java/io/sameboy/android/LibraryUi.kt:76-86`

**Interfaces:**
- Produces: `@Composable fun ReadableContent(content: @Composable () -> Unit)` in `io.sameboy.android.cupertino`.

- [ ] **Step 1: Add `ReadableContent` to Components.kt (end of file)**

```kotlin
/** iOS readable-width idiom: center content in a <=640dp column.
 *  Geometric no-op on screens narrower than the cap (phones). */
@Composable
fun ReadableContent(content: @Composable () -> Unit) {
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
        Box(Modifier.widthIn(max = 640.dp)) { content() }
    }
}
```

(All imports already present in Components.kt.)

- [ ] **Step 2: Wrap the four grouped screens**

Each change wraps the screen's outermost `Column(...)` in `ReadableContent { ... }` and adds `ReadableContent` to that file's cupertino imports.

`SettingsUi.kt` (line 87) — before:
```kotlin
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
```
after (closing brace added at the function's matching end):
```kotlin
    ReadableContent {
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
```

`LinkUi.kt` (line 52) — same pattern around `Column(Modifier.fillMaxSize()) {`.

`RemapUi.kt` (line 47) — same pattern around `Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {`.

`PrinterUi.kt` (line 50) — same pattern around `Column(Modifier.fillMaxSize()) {`.

Indentation of the wrapped body may be left as-is or re-indented — keep the diff minimal (prefer re-indent only the two touched lines; Kotlin doesn't care).

- [ ] **Step 3: Library — Settings becomes a nav action**

`LibraryUi.kt` lines 76-86, before:
```kotlin
    Column(Modifier.fillMaxSize()) {
        CupertinoNavBar(title = "Library")
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            CupertinoButton(stringResource(R.string.import_folder)) { cb.onImportFolder() }
            CupertinoButton(stringResource(R.string.open_rom)) { cb.onOpenRom() }
            Spacer(Modifier.weight(1f))
            CupertinoButton(stringResource(R.string.settings), style = ButtonStyle.Plain) { cb.onSettings() }
        }
```
after:
```kotlin
    Column(Modifier.fillMaxSize()) {
        CupertinoNavBar(title = "Library", trailing = {
            CupertinoButton(stringResource(R.string.settings), style = ButtonStyle.Plain) { cb.onSettings() }
        })
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            CupertinoButton(stringResource(R.string.import_folder)) { cb.onImportFolder() }
            CupertinoButton(stringResource(R.string.open_rom)) { cb.onOpenRom() }
        }
```
Remove the now-unused `Spacer` import ONLY if no other use remains in the file (check; `Spacer` is also used in GameTile — keep it).

- [ ] **Step 4: Compile**

Run: `cd /home/sian/SameBoy/Android && JAVA_HOME=$HOME/Android/jdk17 ./gradlew :app:compileDebugJavaWithJavac`
Expected: `BUILD SUCCESSFUL`

- [ ] **Step 5: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/
git commit -m "feat(android): iOS readable-width cap for grouped screens; Library Settings moves to nav action"
```

---

### Task 2: OnePlus Pad verification (controller-run)

**Files:** none (screenshots to `/tmp/sameboy-ui/pad/`)

Device: OnePlus Pad `adb -s c5df6f52` (OPD2403, Android 16, arm64, 3000×2120 @ 420dpi).

- [ ] **Step 1:** `assembleDebug`, install on the Pad, stage ROM folder if needed.
- [ ] **Step 2 (tablet-native):** screenshot Library (grid full-width, Settings in nav bar), Settings / Link / Remap / Printer (centered 640dp column), light + dark.
- [ ] **Step 3 (phone sim):** `adb -s c5df6f52 shell wm size 1080x2340` (~412dp width @420dpi); walk Library (toolbar fits, two pills + nav Settings), Settings rows usable, game menu sheet; screenshots; then `wm size reset`.
- [ ] **Step 4 (parity smoke):** import/open a ROM, launch, in-game menu opens, Settings reachable from nav action.
- [ ] **Step 5:** review screenshots with the read tool; fix defects; commit fixes.
