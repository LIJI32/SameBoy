# Android Cupertino Compose UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rework every non-game screen of the SameBoy Android port into an iOS (Cupertino) look, implemented in Jetpack Compose, keeping all existing Java logic.

**Architecture:** Hand-rolled Cupertino theme + component kit in Kotlin (`cupertino/` subpackage). Java activities keep their logic (SAF, JNI, executors) and call a Kotlin `XxxUi.bind(activity, callbacks)` bridge that does `setContent`. Dialogs (game menu, palette editor) become Kotlin objects with unchanged public signatures hosted in `ComponentDialog` + `ComposeView`.

**Tech Stack:** Kotlin 2.0.21, Compose BOM 2024.09.03 (foundation + ui only, **no Material3**), activity-compose 1.9.2, AGP 8.5.2, Gradle 8.9, JDK 17.

## Global Constraints

- Build with `JAVA_HOME=$HOME/Android/jdk17` — system JDK 26 is unsupported by Gradle 8.9.
- Working dir for all gradle commands: `/home/sian/SameBoy/Android`.
- compileSdk stays 34; minSdk stays 26; AGP stays 8.5.2; Gradle stays 8.9.
- **No Material/Material3 dependency. No new third-party dependencies.**
- Package: Kotlin UI files live in `app/src/main/java/io/sameboy/android/` (Kotlin compiles from the java source dir) so they can access package-private Java classes (`Settings`, `Library`, `LibraryEntry`, `GamepadMapper`, `NativeBridge`).
- EmulatorActivity, EmulatorSurfaceView, TouchOverlayView, anything under `jni/`: **untouched**.
- No feature changes — visual rework only. Existing toasts, SAF flows, JNI calls, pause/unpause contracts preserved exactly.
- The repo has no test infrastructure and this is a visual rework; per the approved spec, per-task verification = compile, final verification = on-device walk + screenshots (Task 11). Do not add test frameworks.
- Fast compile check per task: `JAVA_HOME=$HOME/Android/jdk17 ./gradlew :app:compileDebugJavaWithJavac` (compiles Kotlin then Java; skips NDK/packaging).

---

### Task 1: Compose toolchain

**Files:**
- Modify: `Android/build.gradle` (root, 3 lines)
- Modify: `Android/app/build.gradle`

**Interfaces:**
- Produces: a build where `.kt` files under `app/src/main/java/` compile with Compose available. All later tasks depend on this.

- [ ] **Step 1: Add Kotlin + Compose plugins to root `Android/build.gradle`**

Replace the whole file with:

```gradle
plugins {
    id "com.android.application" version "8.5.2" apply false
    id "org.jetbrains.kotlin.android" version "2.0.21" apply false
    id "org.jetbrains.kotlin.plugin.compose" version "2.0.21" apply false
}
```

- [ ] **Step 2: Apply plugins + Compose deps in `Android/app/build.gradle`**

Change the plugins block (first line of the file) from `plugins { id "com.android.application" }` to:

```gradle
plugins {
    id "com.android.application"
    id "org.jetbrains.kotlin.android"
    id "org.jetbrains.kotlin.plugin.compose"
}
```

Inside the `android { }` block, after `compileOptions { ... }`, add:

```gradle
    kotlinOptions { jvmTarget = "17" }
    buildFeatures { compose true }
```

Replace the `dependencies { }` block with:

```gradle
dependencies {
    implementation "androidx.appcompat:appcompat:1.7.0"
    implementation platform("androidx.compose:compose-bom:2024.09.03")
    implementation "androidx.compose.foundation:foundation"
    implementation "androidx.activity:activity-compose:1.9.2"

    // kotlin-stdlib-jdk7/jdk8 classes were folded into kotlin-stdlib at 1.8.0;
    // align the stub artifacts to the resolved stdlib (now 2.0.21 via Kotlin plugin).
    constraints {
        implementation("org.jetbrains.kotlin:kotlin-stdlib-jdk7:2.0.21")
        implementation("org.jetbrains.kotlin:kotlin-stdlib-jdk8:2.0.21")
    }
}
```

- [ ] **Step 3: Verify the toolchain builds**

Run: `cd /home/sian/SameBoy/Android && JAVA_HOME=$HOME/Android/jdk17 ./gradlew :app:compileDebugJavaWithJavac`
Expected: `BUILD SUCCESSFUL` (first run downloads Kotlin artifacts).

- [ ] **Step 4: Commit**

```bash
git add Android/build.gradle Android/app/build.gradle
git commit -m "build(android): add Kotlin 2.0.21 + Compose BOM 2024.09 toolchain (foundation only, no Material)"
```

---

### Task 2: Cupertino theme

**Files:**
- Create: `Android/app/src/main/java/io/sameboy/android/cupertino/Theme.kt`

**Interfaces:**
- Produces (used by every later task):
  - `object Cupertino { val colors: CupertinoColors @Composable get; val type: CupertinoType }`
  - `CupertinoColors` fields: `systemBackground, systemGroupedBackground, secondarySystemGroupedBackground, label, secondaryLabel, tertiaryLabel, separator, fill, systemBlue, systemRed, systemGreen, onAccent: Color`
  - `CupertinoType` fields: `largeTitle, title2, headline, body, subheadline, footnote, caption: TextStyle`
  - `@Composable fun CupertinoTheme(content: @Composable () -> Unit)` — resolves light/dark from `isSystemInDarkTheme()` (which follows the app's `AppCompatDelegate` override), paints `systemGroupedBackground` full-screen.
  - `@Composable fun CupText(text: String, style: TextStyle, color: Color = Cupertino.colors.label, maxLines: Int = Int.MAX_VALUE, modifier: Modifier = Modifier)`

- [ ] **Step 1: Write `Theme.kt`**

```kotlin
package io.sameboy.android.cupertino

import androidx.compose.foundation.background
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.text.BasicText
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.sp

/** iOS system colors (UIKit semantic palette), light + dark. */
class CupertinoColors(
    val systemBackground: Color,
    val systemGroupedBackground: Color,
    val secondarySystemGroupedBackground: Color,
    val label: Color,
    val secondaryLabel: Color,
    val tertiaryLabel: Color,
    val separator: Color,
    val fill: Color,
    val systemBlue: Color,
    val systemRed: Color,
    val systemGreen: Color,
    val onAccent: Color,
)

private val Light = CupertinoColors(
    systemBackground = Color(0xFFFFFFFF),
    systemGroupedBackground = Color(0xFFF2F2F7),
    secondarySystemGroupedBackground = Color(0xFFFFFFFF),
    label = Color(0xFF000000),
    secondaryLabel = Color(0x993C3C43),
    tertiaryLabel = Color(0x4D3C3C43),
    separator = Color(0x493C3C43),
    fill = Color(0x33787880),
    systemBlue = Color(0xFF007AFF),
    systemRed = Color(0xFFFF3B30),
    systemGreen = Color(0xFF34C759),
    onAccent = Color(0xFFFFFFFF),
)

private val Dark = CupertinoColors(
    systemBackground = Color(0xFF000000),
    systemGroupedBackground = Color(0xFF000000),
    secondarySystemGroupedBackground = Color(0xFF1C1C1E),
    label = Color(0xFFFFFFFF),
    secondaryLabel = Color(0x99EBEBF5),
    tertiaryLabel = Color(0x4DEBEBF5),
    separator = Color(0xA6545458),
    fill = Color(0x5C787880),
    systemBlue = Color(0xFF0A84FF),
    systemRed = Color(0xFFFF453A),
    systemGreen = Color(0xFF30D158),
    onAccent = Color(0xFFFFFFFF),
)

/** iOS type scale on the system font (SF Pro is not bundleable; metrics match). */
class CupertinoType {
    val largeTitle = TextStyle(fontSize = 34.sp, fontWeight = FontWeight.Bold)
    val title2 = TextStyle(fontSize = 22.sp, fontWeight = FontWeight.Bold)
    val headline = TextStyle(fontSize = 17.sp, fontWeight = FontWeight.SemiBold)
    val body = TextStyle(fontSize = 17.sp, fontWeight = FontWeight.Normal)
    val subheadline = TextStyle(fontSize = 15.sp, fontWeight = FontWeight.Normal)
    val footnote = TextStyle(fontSize = 13.sp, fontWeight = FontWeight.Normal)
    val caption = TextStyle(fontSize = 12.sp, fontWeight = FontWeight.Normal)
}

private val LocalCupertinoColors = staticCompositionLocalOf { Light }
private val CupertinoTypeInstance = CupertinoType()

object Cupertino {
    val colors: CupertinoColors
        @Composable get() = LocalCupertinoColors.current
    val type: CupertinoType get() = CupertinoTypeInstance
}

@Composable
fun CupertinoTheme(content: @Composable () -> Unit) {
    val colors = if (isSystemInDarkTheme()) Dark else Light
    CompositionLocalProvider(LocalCupertinoColors provides colors) {
        Box(Modifier.fillMaxSize().background(colors.systemGroupedBackground)) {
            content()
        }
    }
}

@Composable
fun CupText(
    text: String,
    style: TextStyle,
    color: Color = Cupertino.colors.label,
    maxLines: Int = Int.MAX_VALUE,
    modifier: Modifier = Modifier,
) {
    BasicText(
        text = text,
        style = style.copy(color = color),
        maxLines = maxLines,
        overflow = TextOverflow.Ellipsis,
        modifier = modifier,
    )
}
```

- [ ] **Step 2: Compile**

Run: `JAVA_HOME=$HOME/Android/jdk17 ./gradlew :app:compileDebugJavaWithJavac`
Expected: `BUILD SUCCESSFUL`

- [ ] **Step 3: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/cupertino/Theme.kt
git commit -m "feat(android): Cupertino theme - iOS system colors (light+dark) + type scale"
```

---

### Task 3: Cupertino components

**Files:**
- Create: `Android/app/src/main/java/io/sameboy/android/cupertino/Components.kt`

**Interfaces:**
- Consumes: `Cupertino`, `CupertinoTheme`, `CupText` from Task 2.
- Produces (exact signatures, used by all screen tasks):
  - `@Composable fun CupertinoNavBar(title: String, onBack: (() -> Unit)? = null, trailing: (@Composable () -> Unit)? = null)`
  - `@Composable fun CupertinoSection(header: String? = null, footer: String? = null, rows: List<@Composable () -> Unit>)` — inset grouped card, hairline separators between rows.
  - `@Composable fun NavRow(label: String, value: String? = null, onClick: () -> Unit)`
  - `@Composable fun ToggleRow(label: String, checked: Boolean, onChange: (Boolean) -> Unit)`
  - `@Composable fun SliderRow(label: String, value: Int, min: Int, max: Int, unit: String, onChange: (Int) -> Unit)`
  - `@Composable fun PickerRow(label: String, options: List<String>, selected: Int, onSelect: (Int) -> Unit)` — opens its own action sheet.
  - `class SheetAction(val label: String, val destructive: Boolean = false, val onClick: () -> Unit)`
  - `@Composable fun CupertinoActionSheet(title: String?, actions: List<SheetAction>, cancelLabel: String = "Cancel", onDismiss: () -> Unit)`
  - `@Composable fun ActionSheetContent(title: String?, actions: List<SheetAction>, cancelLabel: String, onDismiss: () -> Unit)` — sheet visuals without the Dialog wrapper (reused by the game menu's ComponentDialog host).
  - `@Composable fun CupertinoAlertShell(title: String, buttons: List<SheetAction>, onDismiss: () -> Unit, content: @Composable () -> Unit)` — centered rounded alert card with horizontal button row.
  - `enum class ButtonStyle { Filled, Tinted, Plain }`
  - `@Composable fun CupertinoButton(text: String, style: ButtonStyle = ButtonStyle.Tinted, enabled: Boolean = true, destructive: Boolean = false, onClick: () -> Unit)`

- [ ] **Step 1: Write `Components.kt`**

```kotlin
package io.sameboy.android.cupertino

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.unit.sp
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties

// ---------- nav bar ----------

@Composable
fun CupertinoNavBar(
    title: String,
    onBack: (() -> Unit)? = null,
    trailing: (@Composable () -> Unit)? = null,
) {
    Column(Modifier.fillMaxWidth().statusBarsPadding()) {
        Row(
            Modifier.fillMaxWidth().height(44.dp).padding(horizontal = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (onBack != null) {
                Row(
                    Modifier
                        .clickable(remember { MutableInteractionSource() }, indication = null, onClick = onBack)
                        .padding(8.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    CupText("\u2039", Cupertino.type.title2, Cupertino.colors.systemBlue)
                    Spacer(Modifier.width(4.dp))
                    CupText("Back", Cupertino.type.body, Cupertino.colors.systemBlue)
                }
            }
            Spacer(Modifier.weight(1f))
            if (trailing != null) trailing()
        }
        CupText(
            title, Cupertino.type.largeTitle,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
        )
    }
}

// ---------- inset grouped section ----------

@Composable
fun CupertinoSection(
    header: String? = null,
    footer: String? = null,
    rows: List<@Composable () -> Unit>,
) {
    Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp)) {
        if (header != null) {
            CupText(
                header.uppercase(), Cupertino.type.footnote, Cupertino.colors.secondaryLabel,
                modifier = Modifier.padding(start = 16.dp, bottom = 6.dp),
            )
        }
        Column(
            Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(10.dp))
                .background(Cupertino.colors.secondarySystemGroupedBackground),
        ) {
            rows.forEachIndexed { i, row ->
                row()
                if (i < rows.lastIndex) {
                    Box(
                        Modifier
                            .fillMaxWidth()
                            .padding(start = 16.dp)
                            .height(0.5.dp)
                            .background(Cupertino.colors.separator),
                    )
                }
            }
        }
        if (footer != null) {
            CupText(
                footer, Cupertino.type.footnote, Cupertino.colors.secondaryLabel,
                modifier = Modifier.padding(start = 16.dp, top = 6.dp),
            )
        }
    }
}

/** 44dp min-height row with iOS press highlight. */
@Composable
private fun RowShell(onClick: (() -> Unit)?, content: @Composable () -> Unit) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    val bg = if (pressed && onClick != null) Cupertino.colors.fill else Color.Transparent
    Row(
        Modifier
            .fillMaxWidth()
            .defaultMinSize(minHeight = 44.dp)
            .background(bg)
            .let { if (onClick != null) it.clickable(interaction, indication = null, onClick = onClick) else it }
            .padding(horizontal = 16.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) { content() }
}

@Composable
fun NavRow(label: String, value: String? = null, onClick: () -> Unit) {
    RowShell(onClick) {
        CupText(label, Cupertino.type.body, maxLines = 1, modifier = Modifier.weight(1f))
        if (value != null) {
            CupText(value, Cupertino.type.body, Cupertino.colors.secondaryLabel, maxLines = 1)
            Spacer(Modifier.width(6.dp))
        }
        CupText("\u203A", Cupertino.type.body, Cupertino.colors.tertiaryLabel)
    }
}

// ---------- switch ----------

@Composable
fun CupertinoSwitch(checked: Boolean, onChange: (Boolean) -> Unit) {
    val track by animateColorAsState(
        if (checked) Cupertino.colors.systemGreen else Cupertino.colors.fill, label = "track",
    )
    val thumbX by animateDpAsState(if (checked) 22.dp else 2.dp, label = "thumb")
    Box(
        Modifier
            .size(51.dp, 31.dp)
            .clip(RoundedCornerShape(15.5.dp))
            .background(track)
            .clickable(remember { MutableInteractionSource() }, indication = null) { onChange(!checked) },
    ) {
        Box(
            Modifier
                .offset(x = thumbX)
                .align(Alignment.CenterStart)
                .size(27.dp)
                .shadow(2.dp, CircleShape)
                .background(Color.White, CircleShape),
        )
    }
}

@Composable
fun ToggleRow(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    RowShell(null) {
        CupText(label, Cupertino.type.body, maxLines = 1, modifier = Modifier.weight(1f))
        CupertinoSwitch(checked, onChange)
    }
}

// ---------- slider ----------

@Composable
fun CupertinoSlider(value: Float, onValueChange: (Float) -> Unit, modifier: Modifier = Modifier) {
    val thumb = 28.dp
    val density = LocalDensity.current
    var widthPx by remember { mutableFloatStateOf(0f) }
    val thumbPx = with(density) { thumb.toPx() }
    fun set(x: Float) {
        val usable = (widthPx - thumbPx).coerceAtLeast(1f)
        onValueChange(((x - thumbPx / 2) / usable).coerceIn(0f, 1f))
    }
    Box(
        modifier
            .fillMaxWidth()
            .height(thumb + 8.dp)
            .onSizeChanged { widthPx = it.width.toFloat() }
            .pointerInput(Unit) { detectTapGestures { set(it.x) } }
            .pointerInput(Unit) {
                detectDragGestures { change, _ -> change.consume(); set(change.position.x) }
            },
        contentAlignment = Alignment.CenterStart,
    ) {
        Box(
            Modifier.fillMaxWidth().height(4.dp)
                .clip(RoundedCornerShape(2.dp)).background(Cupertino.colors.fill),
        )
        val frac = value.coerceIn(0f, 1f)
        Box(
            Modifier
                .fillMaxWidth(frac).height(4.dp)
                .clip(RoundedCornerShape(2.dp)).background(Cupertino.colors.systemBlue),
        )
        Box(
            Modifier
                .offset(x = with(density) { ((widthPx - thumbPx) * frac).toDp() })
                .size(thumb)
                .shadow(3.dp, CircleShape)
                .background(Color.White, CircleShape),
        )
    }
}

@Composable
fun SliderRow(label: String, value: Int, min: Int, max: Int, unit: String, onChange: (Int) -> Unit) {
    Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            CupText(label, Cupertino.type.body, maxLines = 1, modifier = Modifier.weight(1f))
            CupText("$value$unit", Cupertino.type.subheadline, Cupertino.colors.secondaryLabel)
        }
        CupertinoSlider(
            value = (value - min).toFloat() / (max - min).coerceAtLeast(1),
            onValueChange = { onChange(min + ((max - min) * it + 0.5f).toInt()) },
        )
    }
}

// ---------- picker row (opens action sheet) ----------

@Composable
fun PickerRow(label: String, options: List<String>, selected: Int, onSelect: (Int) -> Unit) {
    var open by remember { mutableStateOf(false) }
    NavRow(label, options.getOrNull(selected) ?: "", onClick = { open = true })
    if (open) {
        CupertinoActionSheet(
            title = label,
            actions = options.mapIndexed { i, opt -> SheetAction(opt) { onSelect(i) } },
            onDismiss = { open = false },
        )
    }
}

// ---------- action sheet ----------

class SheetAction(val label: String, val destructive: Boolean = false, val onClick: () -> Unit)

@Composable
private fun SheetButton(label: String, color: Color, weight: FontWeight, onClick: () -> Unit) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    Box(
        Modifier
            .fillMaxWidth().height(57.dp)
            .background(if (pressed) Cupertino.colors.fill else Color.Transparent)
            .clickable(interaction, indication = null, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        CupText(label, Cupertino.type.body.copy(fontSize = 20.sp, fontWeight = weight), color, maxLines = 1)
    }
}

@Composable
fun ActionSheetContent(
    title: String?,
    actions: List<SheetAction>,
    cancelLabel: String,
    onDismiss: () -> Unit,
) {
    Column(Modifier.widthIn(max = 420.dp).fillMaxWidth().padding(8.dp)) {
        Column(
            Modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp))
                .background(Cupertino.colors.secondarySystemGroupedBackground),
        ) {
            if (title != null) {
                Box(Modifier.fillMaxWidth().padding(vertical = 12.dp), contentAlignment = Alignment.Center) {
                    CupText(title, Cupertino.type.footnote, Cupertino.colors.secondaryLabel)
                }
                Box(Modifier.fillMaxWidth().height(0.5.dp).background(Cupertino.colors.separator))
            }
            actions.forEachIndexed { i, a ->
                SheetButton(
                    a.label,
                    if (a.destructive) Cupertino.colors.systemRed else Cupertino.colors.systemBlue,
                    FontWeight.Normal,
                ) { a.onClick(); onDismiss() }
                if (i < actions.lastIndex) {
                    Box(Modifier.fillMaxWidth().height(0.5.dp).background(Cupertino.colors.separator))
                }
            }
        }
        Spacer(Modifier.height(8.dp))
        Column(
            Modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp))
                .background(Cupertino.colors.secondarySystemGroupedBackground),
        ) {
            SheetButton(cancelLabel, Cupertino.colors.systemBlue, FontWeight.SemiBold, onDismiss)
        }
    }
}

@Composable
fun CupertinoActionSheet(
    title: String?,
    actions: List<SheetAction>,
    cancelLabel: String = "Cancel",
    onDismiss: () -> Unit,
) {
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Box(
            Modifier.fillMaxSize()
                .clickable(remember { MutableInteractionSource() }, indication = null, onClick = onDismiss),
            contentAlignment = Alignment.BottomCenter,
        ) {
            ActionSheetContent(title, actions, cancelLabel, onDismiss)
        }
    }
}

// ---------- alert shell (centered card) ----------

@Composable
fun CupertinoAlertShell(
    title: String,
    buttons: List<SheetAction>,
    onDismiss: () -> Unit,
    content: @Composable () -> Unit,
) {
    Dialog(onDismissRequest = onDismiss) {
        Column(
            Modifier.widthIn(max = 340.dp).clip(RoundedCornerShape(14.dp))
                .background(Cupertino.colors.secondarySystemGroupedBackground),
        ) {
            CupText(
                title, Cupertino.type.headline,
                modifier = Modifier.padding(16.dp).fillMaxWidth(),
            )
            content()
            Box(Modifier.fillMaxWidth().height(0.5.dp).background(Cupertino.colors.separator))
            Row(Modifier.fillMaxWidth()) {
                buttons.forEachIndexed { i, b ->
                    Box(
                        Modifier.weight(1f).height(44.dp)
                            .clickable(remember { MutableInteractionSource() }, indication = null) {
                                b.onClick()
                            },
                        contentAlignment = Alignment.Center,
                    ) {
                        CupText(
                            b.label, Cupertino.type.body,
                            if (b.destructive) Cupertino.colors.systemRed else Cupertino.colors.systemBlue,
                        )
                    }
                    if (i < buttons.lastIndex) {
                        Box(Modifier.width(0.5.dp).height(44.dp).background(Cupertino.colors.separator))
                    }
                }
            }
        }
    }
}

// ---------- buttons ----------

enum class ButtonStyle { Filled, Tinted, Plain }

@Composable
fun CupertinoButton(
    text: String,
    style: ButtonStyle = ButtonStyle.Tinted,
    enabled: Boolean = true,
    destructive: Boolean = false,
    onClick: () -> Unit,
) {
    val accent = if (destructive) Cupertino.colors.systemRed else Cupertino.colors.systemBlue
    val bg = when (style) {
        ButtonStyle.Filled -> accent
        ButtonStyle.Tinted -> Cupertino.colors.fill
        ButtonStyle.Plain -> Color.Transparent
    }
    val fg = when (style) {
        ButtonStyle.Filled -> Cupertino.colors.onAccent
        else -> accent
    }
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    Box(
        Modifier
            .alpha(if (pressed) 0.5f else 1f)
            .clip(RoundedCornerShape(50))
            .background(if (enabled) bg else Cupertino.colors.fill)
            .clickable(interaction, indication = null, enabled = enabled, onClick = onClick)
            .padding(horizontal = 16.dp, vertical = 8.dp),
        contentAlignment = Alignment.Center,
    ) {
        CupText(
            text, Cupertino.type.body.copy(fontWeight = FontWeight.Medium),
            if (enabled) fg else Cupertino.colors.tertiaryLabel, maxLines = 1,
        )
    }
}
```

(`sp` is used by SheetButton; `alpha` by CupertinoButton.)

- [ ] **Step 2: Compile**

Run: `JAVA_HOME=$HOME/Android/jdk17 ./gradlew :app:compileDebugJavaWithJavac`
Expected: `BUILD SUCCESSFUL`

- [ ] **Step 3: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/cupertino/Components.kt
git commit -m "feat(android): Cupertino component kit - nav bar, grouped sections, switch, slider, action sheet, alert, buttons"
```

---

### Task 4: Library screen

**Files:**
- Create: `Android/app/src/main/java/io/sameboy/android/LibraryUi.kt`
- Modify: `Android/app/src/main/java/io/sameboy/android/MainActivity.java`

**Interfaces:**
- Consumes: Task 2–3 components; Java `LibraryEntry` (fields `favorite`, `lastPlayed`, method `label()`).
- Produces:
  - `object LibraryUi` with `@JvmStatic fun bind(activity: ComponentActivity, cb: Callbacks): Model`
  - `LibraryUi.Model` with `fun setGames(list: List<LibraryEntry>)`
  - `LibraryUi.Callbacks`: `onImportFolder()`, `onOpenRom()`, `onSettings()`, `onPlay(e: LibraryEntry)`, `onToggleFavorite(e: LibraryEntry)`, `onRemove(e: LibraryEntry)`

- [ ] **Step 1: Write `LibraryUi.kt`**

```kotlin
package io.sameboy.android

import android.text.format.DateUtils
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import io.sameboy.android.cupertino.ButtonStyle
import io.sameboy.android.cupertino.CupText
import io.sameboy.android.cupertino.Cupertino
import io.sameboy.android.cupertino.CupertinoActionSheet
import io.sameboy.android.cupertino.CupertinoButton
import io.sameboy.android.cupertino.CupertinoNavBar
import io.sameboy.android.cupertino.CupertinoTheme
import io.sameboy.android.cupertino.SheetAction

object LibraryUi {
    class Model internal constructor() {
        internal val games = mutableStateListOf<LibraryEntry>()
        fun setGames(list: List<LibraryEntry>) { games.clear(); games.addAll(list) }
    }

    interface Callbacks {
        fun onImportFolder()
        fun onOpenRom()
        fun onSettings()
        fun onPlay(e: LibraryEntry)
        fun onToggleFavorite(e: LibraryEntry)
        fun onRemove(e: LibraryEntry)
    }

    @JvmStatic
    fun bind(activity: ComponentActivity, cb: Callbacks): Model {
        val model = Model()
        activity.setContent { CupertinoTheme { LibraryScreen(model, cb) } }
        return model
    }
}

@Composable
private fun LibraryScreen(model: LibraryUi.Model, cb: LibraryUi.Callbacks) {
    var context by remember { mutableStateOf<LibraryEntry?>(null) }
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
        if (model.games.isEmpty()) {
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                CupText(
                    stringResource(R.string.library_empty),
                    Cupertino.type.subheadline, Cupertino.colors.secondaryLabel,
                )
            }
        } else {
            LazyVerticalGrid(
                columns = GridCells.Adaptive(150.dp),
                contentPadding = androidx.compose.foundation.layout.PaddingValues(16.dp),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
                modifier = Modifier.fillMaxSize(),
            ) {
                items(model.games, key = { it.crc32 }) { e ->
                    GameTile(e, onClick = { cb.onPlay(e) }, onLongClick = { context = e })
                }
            }
        }
    }
    context?.let { e ->
        CupertinoActionSheet(
            title = e.label(),
            actions = listOf(
                SheetAction(stringResource(R.string.play)) { cb.onPlay(e) },
                SheetAction(stringResource(if (e.favorite) R.string.unfavorite else R.string.favorite)) {
                    cb.onToggleFavorite(e)
                },
                SheetAction(stringResource(R.string.remove), destructive = true) { cb.onRemove(e) },
            ),
            onDismiss = { context = null },
        )
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun GameTile(e: LibraryEntry, onClick: () -> Unit, onLongClick: () -> Unit) {
    Column(
        Modifier
            .clip(RoundedCornerShape(12.dp))
            .background(Cupertino.colors.secondarySystemGroupedBackground)
            .combinedClickable(onClick = onClick, onLongClick = onLongClick)
            .padding(12.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            if (e.favorite) {
                CupText("\u2605", Cupertino.type.headline, Cupertino.colors.systemBlue)
                Spacer(Modifier.width(4.dp))
            }
            CupText(e.label(), Cupertino.type.headline, maxLines = 1)
        }
        Spacer(Modifier.height(4.dp))
        CupText(
            if (e.lastPlayed == 0L) stringResource(R.string.never)
            else DateUtils.getRelativeTimeSpanString(e.lastPlayed).toString(),
            Cupertino.type.footnote, Cupertino.colors.secondaryLabel, maxLines = 1,
        )
    }
}
```

- [ ] **Step 2: Rewrite `MainActivity.java`**

Full new content (logic identical; view-building + adapter + `showContext` replaced by the Compose bridge):

```java
package io.sameboy.android;

import androidx.appcompat.app.AppCompatActivity;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.widget.Toast;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MainActivity extends AppCompatActivity {
    private static final int REQ_TREE = 1, REQ_FILE = 2;
    private final ExecutorService io = Executors.newSingleThreadExecutor();
    private final Handler ui = new Handler(Looper.getMainLooper());
    private Library library;
    private LibraryUi.Model model;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        library = new Library(this);
        library.load();
        model = LibraryUi.bind(this, new LibraryUi.Callbacks() {
            @Override public void onImportFolder() { pickTree(); }
            @Override public void onOpenRom() { pickFile(); }
            @Override public void onSettings() {
                startActivity(new Intent(MainActivity.this, SettingsActivity.class));
            }
            @Override public void onPlay(LibraryEntry e) { launch(e); }
            @Override public void onToggleFavorite(LibraryEntry e) {
                library.setFavorite(e.crc32, !e.favorite); library.save(); refresh();
            }
            @Override public void onRemove(LibraryEntry e) {
                library.remove(e.crc32); library.save(); refresh();
            }
        });
    }

    @Override protected void onResume() {
        super.onResume();
        library.load();   // pick up a background scan's save / another instance's changes
        refresh();
    }

    private void refresh() { model.setGames(library.listSorted()); }

    private void pickTree() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(i, REQ_TREE);
    }
    private void pickFile() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(i, REQ_FILE);
    }

    @Override protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (res != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        int grant = data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION;
        try { getContentResolver().takePersistableUriPermission(uri, grant); } catch (Exception ignored) {}

        if (req == REQ_TREE) {
            Toast.makeText(this, R.string.scanning, Toast.LENGTH_SHORT).show();
            io.execute(() -> {
                int[] added = {0};
                RomScanner.scanTree(this, uri, entry ->
                    ui.post(() -> { if (library.add(entry)) added[0]++; }));
                ui.post(() -> {
                    library.save();
                    refresh();
                    Toast.makeText(this, getString(R.string.added_n, added[0]), Toast.LENGTH_SHORT).show();
                });
            });
        } else { // REQ_FILE
            io.execute(() -> {
                String name = queryName(uri);   // SAF query off the main thread (ANR-safe)
                boolean[] got = {false};
                RomScanner.handleFile(getContentResolver(), uri, name, entry -> {
                    got[0] = true;
                    ui.post(() -> { library.add(entry); library.save(); refresh(); launch(entry); });
                });
                if (!got[0]) ui.post(() -> Toast.makeText(this, R.string.not_a_rom, Toast.LENGTH_SHORT).show());
            });
        }
    }

    private void launch(LibraryEntry entry) {
        library.touch(entry.crc32);
        library.save();
        refresh();
        Intent i = new Intent(this, EmulatorActivity.class);
        i.setData(Uri.parse(entry.uri));
        if (entry.zipEntry != null) i.putExtra(EmulatorActivity.EXTRA_ZIP_ENTRY, entry.zipEntry);
        i.putExtra(EmulatorActivity.EXTRA_ROM_KEY, entry.crc32);
        startActivity(i);
    }

    private String queryName(Uri uri) {
        String name = null;
        try (Cursor c = getContentResolver().query(uri,
                new String[]{ OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                int i = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (i >= 0) name = c.getString(i);
            }
        } catch (Exception ignored) {}
        if (name == null) name = uri.getLastPathSegment();
        return name == null ? "rom" : name;
    }
}
```

- [ ] **Step 3: Compile**

Run: `JAVA_HOME=$HOME/Android/jdk17 ./gradlew :app:compileDebugJavaWithJavac`
Expected: `BUILD SUCCESSFUL`

- [ ] **Step 4: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/LibraryUi.kt Android/app/src/main/java/io/sameboy/android/MainActivity.java
git commit -m "feat(android): Cupertino library screen - large title, game tile grid, long-press action sheet"
```

---

### Task 5: Settings screen (+ palette editor)

**Files:**
- Create: `Android/app/src/main/java/io/sameboy/android/SettingsUi.kt`
- Modify: `Android/app/src/main/java/io/sameboy/android/SettingsActivity.java` (shrinks to a thin shell)
- Delete: `Android/app/src/main/java/io/sameboy/android/PaletteEditorDialog.java` (becomes a composable inside SettingsUi.kt; its only caller was SettingsActivity)

**Interfaces:**
- Consumes: Task 2–3 components; Java `Settings` (all getters/setters incl. `customColor(i)`, `setCustomColor(i, rgb)`, `setPaletteBuiltin(v)`, `applyTheme()`); `NativeBridge.MODEL_DMG_B/MODEL_CGB_E/MODEL_AGB` constants.
- Produces: `object SettingsUi` with `@JvmStatic fun bind(activity: ComponentActivity, s: Settings)`.

- [ ] **Step 1: Write `SettingsUi.kt`**

```kotlin
package io.sameboy.android

import android.content.Intent
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import io.sameboy.android.cupertino.CupText
import io.sameboy.android.cupertino.Cupertino
import io.sameboy.android.cupertino.CupertinoAlertShell
import io.sameboy.android.cupertino.CupertinoNavBar
import io.sameboy.android.cupertino.CupertinoSection
import io.sameboy.android.cupertino.CupertinoSlider
import io.sameboy.android.cupertino.CupertinoTheme
import io.sameboy.android.cupertino.NavRow
import io.sameboy.android.cupertino.PickerRow
import io.sameboy.android.cupertino.SheetAction
import io.sameboy.android.cupertino.SliderRow
import io.sameboy.android.cupertino.ToggleRow

object SettingsUi {
    @JvmStatic
    fun bind(activity: ComponentActivity, s: Settings) {
        activity.setContent {
            CupertinoTheme {
                SettingsScreen(
                    s,
                    onBack = { activity.finish() },
                    onGamepad = {
                        activity.startActivity(Intent(activity, GamepadRemapActivity::class.java))
                    },
                )
            }
        }
    }
}

private val MODELS = intArrayOf(
    NativeBridge.MODEL_DMG_B, NativeBridge.MODEL_CGB_E, NativeBridge.MODEL_AGB,
)
private fun modelToIndex(m: Int) = MODELS.indexOf(m).let { if (it < 0) 1 else it }  // unknown -> CGB (old default)

@Composable
private fun SettingsScreen(s: Settings, onBack: () -> Unit, onGamepad: () -> Unit) {
    var model by remember { mutableIntStateOf(modelToIndex(s.model())) }
    var rewind by remember { mutableIntStateOf(s.rewindSeconds()) }
    var rtc by remember { mutableIntStateOf(s.rtcMode()) }
    var turbo by remember { mutableIntStateOf(s.turboCapQuarters()) }
    var color by remember { mutableIntStateOf(s.colorCorrection()) }
    var light by remember { mutableIntStateOf(s.lightSlider()) }
    var border by remember { mutableIntStateOf(s.borderMode()) }
    var palette by remember { mutableIntStateOf(s.paletteBuiltin()) }
    var paletteEditor by remember { mutableStateOf(false) }
    var volume by remember { mutableIntStateOf(s.volumePct()) }
    var highpass by remember { mutableIntStateOf(s.highpass()) }
    var interference by remember { mutableIntStateOf(s.interferencePct()) }
    var opacity by remember { mutableIntStateOf(s.buttonOpacityPct()) }
    var haptics by remember { mutableStateOf(s.haptics()) }
    var rumble by remember { mutableIntStateOf(s.rumbleMode()) }
    var theme by remember { mutableIntStateOf(s.themeMode()) }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        CupertinoNavBar(title = stringResource(R.string.settings), onBack = onBack)

        CupertinoSection(header = "Emulation", rows = listOf(
            {
                PickerRow(
                    "Model (next launch)",
                    listOf("Game Boy (DMG)", "Game Boy Color (CGB)", "Game Boy Advance (AGB)"),
                    model,
                ) { model = it; s.setModel(MODELS[it]) }
            },
            { SliderRow("Rewind length", rewind, 0, 600, " s") { rewind = it; s.setRewindSeconds(it) } },
            {
                PickerRow("RTC mode", listOf("Sync to host", "Accurate"), rtc) {
                    rtc = it; s.setRtcMode(it)
                }
            },
            {
                SliderRow("Turbo cap (0 = uncapped)", turbo, 0, 32, " /4x") {
                    turbo = it; s.setTurboCapQuarters(it)
                }
            },
        ))

        CupertinoSection(header = "Video", rows = listOf(
            {
                PickerRow(
                    "Color correction",
                    listOf("Disabled", "Correct Curves", "Modern Balanced", "Modern Boost Contrast",
                           "Reduce Contrast", "Low Contrast", "Modern Accurate"),
                    color,
                ) { color = it; s.setColorCorrection(it) }
            },
            { SliderRow("Light temperature", light, 0, 20, "") { light = it; s.setLightSlider(it) } },
            {
                PickerRow("Border", listOf("SGB", "Never", "Always"), border) {
                    border = it; s.setBorderMode(it)
                }
            },
            {
                val names = listOf("Greyscale", "DMG", "MGB", "GBL", "Custom\u2026")
                PickerRow(
                    stringResource(R.string.palette), names,
                    if (palette < 0) 4 else palette.coerceIn(0, 3),
                ) { i ->
                    if (i == 4) paletteEditor = true
                    else { palette = i; s.setPaletteBuiltin(i) }
                }
            },
        ))

        CupertinoSection(header = "Audio", rows = listOf(
            { SliderRow("Volume", volume, 0, 100, " %") { volume = it; s.setVolumePct(it) } },
            {
                PickerRow("High-pass filter", listOf("Off", "Accurate", "Remove DC offset"), highpass) {
                    highpass = it; s.setHighpass(it)
                }
            },
            {
                SliderRow("Interference", interference, 0, 100, " %") {
                    interference = it; s.setInterferencePct(it)
                }
            },
        ))

        CupertinoSection(header = "Controls", rows = listOf(
            {
                SliderRow("Button opacity", opacity, 0, 100, " %") {
                    opacity = it; s.setButtonOpacityPct(it)
                }
            },
            { ToggleRow("Haptics", haptics) { haptics = it; s.setHaptics(it) } },
            {
                PickerRow(
                    stringResource(R.string.rumble),
                    listOf("Disabled", "Cartridge only", "All games"), rumble,
                ) { rumble = it; s.setRumbleMode(it) }
            },
            { NavRow(stringResource(R.string.gamepad_buttons), onClick = onGamepad) },
        ))

        CupertinoSection(header = "Appearance", rows = listOf(
            {
                PickerRow(stringResource(R.string.theme), listOf("System", "Light", "Dark"), theme) {
                    theme = it; s.setThemeMode(it); s.applyTheme()
                }
            },
        ))
    }

    if (paletteEditor) {
        PaletteEditorSheet(s, onDismiss = { paletteEditor = false }, onApplied = {
            palette = -1
            paletteEditor = false
        })
    }
}

/** Custom 4-shade DMG palette editor. Shade 0 = darkest .. 3 = lightest. */
@Composable
private fun PaletteEditorSheet(s: Settings, onDismiss: () -> Unit, onApplied: () -> Unit) {
    val colors = remember { (0..3).map { mutableIntStateOf(s.customColor(it)) } }
    CupertinoAlertShell(
        title = "Custom palette",
        buttons = listOf(
            SheetAction("Cancel") { onDismiss() },
            SheetAction("Apply") {
                for (i in 0..3) s.setCustomColor(i, colors[i].intValue)
                s.setPaletteBuiltin(-1)
                onApplied()
            },
        ),
        onDismiss = onDismiss,
    ) {
        Column(Modifier.padding(horizontal = 16.dp)) {
            val labels = listOf("Shade 0 (darkest)", "Shade 1", "Shade 2", "Shade 3 (lightest)")
            for (i in 0..3) {
                var rgb by colors[i]
                CupText(labels[i], Cupertino.type.subheadline, Cupertino.colors.secondaryLabel)
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(
                        Modifier.size(40.dp).clip(RoundedCornerShape(8.dp))
                            .background(Color(0xFF000000.toInt() or rgb)),
                    )
                    Spacer(Modifier.width(12.dp))
                    Column(Modifier.fillMaxWidth()) {
                        for (shift in intArrayOf(16, 8, 0)) {
                            CupertinoSlider(
                                value = ((rgb shr shift) and 0xFF) / 255f,
                                onValueChange = { v ->
                                    val ch = (v * 255 + 0.5f).toInt().coerceIn(0, 255)
                                    rgb = (rgb and (0xFF shl shift).inv()) or (ch shl shift)
                                },
                            )
                        }
                    }
                }
                Spacer(Modifier.padding(4.dp))
            }
        }
    }
}
```

- [ ] **Step 2: Shrink `SettingsActivity.java`**

Full new content:

```java
package io.sameboy.android;

import androidx.appcompat.app.AppCompatActivity;
import android.os.Bundle;

/** Thin shell: all UI lives in SettingsUi (Compose, Cupertino look).
 *  Writes SharedPreferences via Settings; EmulatorActivity applies on resume. */
public class SettingsActivity extends AppCompatActivity {
    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        SettingsUi.bind(this, new Settings(this));
    }
}
```

- [ ] **Step 3: Delete `PaletteEditorDialog.java`**

```bash
git rm Android/app/src/main/java/io/sameboy/android/PaletteEditorDialog.java
```

- [ ] **Step 4: Compile**

Run: `JAVA_HOME=$HOME/Android/jdk17 ./gradlew :app:compileDebugJavaWithJavac`
Expected: `BUILD SUCCESSFUL`

- [ ] **Step 5: Commit**

```bash
git add -A Android/app/src/main/java/io/sameboy/android/
git commit -m "feat(android): Cupertino settings screen - grouped sections, pickers/sliders/toggles, palette editor alert"
```

---

### Task 6: Game menu + save slots

**Files:**
- Delete: `Android/app/src/main/java/io/sameboy/android/GameMenuDialog.java`
- Create: `Android/app/src/main/java/io/sameboy/android/GameMenuDialog.kt` (same public API — `EmulatorActivity` line 410 `GameMenuDialog.show(this, new GameMenuDialog.Host() {...})` must keep compiling **unchanged**)

**Interfaces:**
- Consumes: Task 2–3; `NativeBridge.MODEL_*`; `R.string.accessory_none/accessory_printer/connect_accessory`.
- Produces: `object GameMenuDialog` with `interface Host` (methods exactly as the Java original: `onMenuClosed()`, `onSaveSlot(int)`, `onLoadSlot(int)`, `onResetGame()`, `onSwitchModel(int)`, `onOpenSettings()`, `onConnectAccessory(int)`, `onPrinterFeed()`, `onLinkCable()`, `printerConnected(): Boolean`, `onExitGame()`, `stateFile(int): File`, `thumbnail(int): Bitmap?`) and `@JvmStatic fun show(a: Activity, h: Host)`.
- Behavior contract (from the Java original, MUST be preserved): host pauses before `show()`; `onMenuClosed()` fires on any dismiss **except** when Settings / Link cable / Printer feed / Exit took over (EmulatorActivity re-applies on resume).

- [ ] **Step 1: Write `GameMenuDialog.kt`, delete the `.java`**

```bash
git rm Android/app/src/main/java/io/sameboy/android/GameMenuDialog.java
```

```kotlin
package io.sameboy.android

import android.app.Activity
import android.graphics.Bitmap
import android.graphics.Color as AColor
import android.graphics.drawable.ColorDrawable
import android.text.format.DateUtils
import android.view.Gravity
import android.view.ViewGroup
import androidx.activity.ComponentDialog
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.unit.dp
import io.sameboy.android.cupertino.ActionSheetContent
import io.sameboy.android.cupertino.CupText
import io.sameboy.android.cupertino.Cupertino
import io.sameboy.android.cupertino.CupertinoTheme
import io.sameboy.android.cupertino.SheetAction
import java.io.File

/** In-game menu + save/load slot picker, Cupertino action-sheet style.
 *  The host pauses emulation before show() and unpauses in onMenuClosed(). */
object GameMenuDialog {
    const val SLOTS = 4

    interface Host {
        fun onMenuClosed()
        fun onSaveSlot(slot: Int)
        fun onLoadSlot(slot: Int)
        fun onResetGame()
        fun onSwitchModel(model: Int)
        fun onOpenSettings()
        fun onConnectAccessory(which: Int)   // 0 = None, 1 = Printer
        fun onPrinterFeed()
        fun onLinkCable()
        fun printerConnected(): Boolean
        fun onExitGame()
        fun stateFile(slot: Int): File
        fun thumbnail(slot: Int): Bitmap?
    }

    private enum class Screen { Menu, SaveSlots, LoadSlots, Models, Accessory }

    @JvmStatic
    fun show(a: Activity, h: Host) {
        val chained = booleanArrayOf(false)   // an Activity took over; don't unpause yet
        val dialog = ComponentDialog(a)
        dialog.setContentView(ComposeView(a).apply {
            layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT,
            )
            setContent {
                CupertinoTheme {
                    MenuContent(
                        h,
                        dismiss = { dialog.dismiss() },
                        takeOver = { chained[0] = true; dialog.dismiss() },
                    )
                }
            }
        })
        dialog.window?.apply {
            setBackgroundDrawable(ColorDrawable(AColor.TRANSPARENT))
            setGravity(Gravity.BOTTOM)
            setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
        }
        dialog.setOnDismissListener { if (!chained[0]) h.onMenuClosed() }
        dialog.show()
    }

    @Composable
    private fun MenuContent(h: Host, dismiss: () -> Unit, takeOver: () -> Unit) {
        var screen by remember { mutableStateOf(Screen.Menu) }
        when (screen) {
            Screen.Menu -> ActionSheetContent(
                title = "SameBoy",
                actions = listOf(
                    SheetAction("Save state") { screen = Screen.SaveSlots },
                    SheetAction("Load state") { screen = Screen.LoadSlots },
                    SheetAction("Reset") { h.onResetGame(); dismiss() },
                    SheetAction("Model") { screen = Screen.Models },
                    SheetAction("Connect accessory") { screen = Screen.Accessory },
                    SheetAction("Printer feed") { h.onPrinterFeed(); takeOver() },
                    SheetAction("Link cable") { h.onLinkCable(); takeOver() },
                    SheetAction("Settings") { h.onOpenSettings(); takeOver() },
                    SheetAction("Exit", destructive = true) { h.onExitGame(); takeOver() },
                ),
                cancelLabel = "Resume",
                onDismiss = dismiss,
            )
            Screen.Models -> ActionSheetContent(
                title = "Model (reboots the game)",
                actions = listOf(
                    SheetAction("Game Boy (DMG)") { h.onSwitchModel(NativeBridge.MODEL_DMG_B); dismiss() },
                    SheetAction("Game Boy Color (CGB)") { h.onSwitchModel(NativeBridge.MODEL_CGB_E); dismiss() },
                    SheetAction("Game Boy Advance (AGB)") { h.onSwitchModel(NativeBridge.MODEL_AGB); dismiss() },
                ),
                cancelLabel = "Cancel",
                onDismiss = dismiss,
            )
            Screen.Accessory -> {
                val connected = h.printerConnected()
                ActionSheetContent(
                    title = null,
                    actions = listOf(
                        SheetAction(
                            (if (!connected) "\u2713 " else "") + "None",
                        ) { h.onConnectAccessory(0); dismiss() },
                        SheetAction(
                            (if (connected) "\u2713 " else "") + "Game Boy Printer",
                        ) { h.onConnectAccessory(1); dismiss() },
                    ),
                    cancelLabel = "Cancel",
                    onDismiss = dismiss,
                )
            }
            Screen.SaveSlots -> SlotSheet(h, forSave = true, dismiss = dismiss)
            Screen.LoadSlots -> SlotSheet(h, forSave = false, dismiss = dismiss)
        }
    }

    @Composable
    private fun SlotSheet(h: Host, forSave: Boolean, dismiss: () -> Unit) {
        Column(Modifier.fillMaxWidth().padding(8.dp)) {
            Column(
                Modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp))
                    .background(Cupertino.colors.secondarySystemGroupedBackground)
                    .padding(12.dp),
            ) {
                CupText(
                    if (forSave) "Save to slot" else "Load from slot",
                    Cupertino.type.headline,
                    modifier = Modifier.padding(bottom = 8.dp),
                )
                for (r in 0 until SLOTS step 2) {
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        for (slot in r until minOf(r + 2, SLOTS)) {
                            SlotCard(h, slot, forSave, dismiss, Modifier.weight(1f))
                        }
                    }
                    Spacer(Modifier.height(8.dp))
                }
            }
            Spacer(Modifier.height(8.dp))
            Column(
                Modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp))
                    .background(Cupertino.colors.secondarySystemGroupedBackground)
                    .clickable(remember { MutableInteractionSource() }, indication = null, onClick = dismiss)
                    .height(57.dp),
                verticalArrangement = Arrangement.Center,
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                CupText("Cancel", Cupertino.type.headline, Cupertino.colors.systemBlue)
            }
        }
    }

    @Composable
    private fun SlotCard(h: Host, slot: Int, forSave: Boolean, dismiss: () -> Unit, modifier: Modifier) {
        val file = h.stateFile(slot)
        val exists = file.exists()
        val enabled = forSave || exists
        val bmp = if (exists) h.thumbnail(slot) else null
        Row(
            modifier
                .clip(RoundedCornerShape(10.dp))
                .background(Cupertino.colors.fill)
                .let {
                    if (enabled) it.clickable(remember { MutableInteractionSource() }, indication = null) {
                        if (forSave) h.onSaveSlot(slot) else h.onLoadSlot(slot)
                        dismiss()
                    } else it
                }
                .alpha(if (enabled) 1f else 0.4f)
                .padding(8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(Modifier.size(64.dp, 58.dp).clip(RoundedCornerShape(6.dp)).background(Cupertino.colors.separator)) {
                if (bmp != null) {
                    Image(
                        bitmap = bmp.asImageBitmap(), contentDescription = null,
                        contentScale = ContentScale.Fit, modifier = Modifier.size(64.dp, 58.dp),
                    )
                }
            }
            Spacer(Modifier.width(8.dp))
            Column {
                CupText("Slot ${slot + 1}", Cupertino.type.body, maxLines = 1)
                CupText(
                    if (exists) DateUtils.getRelativeTimeSpanString(file.lastModified()).toString() else "Empty",
                    Cupertino.type.footnote, Cupertino.colors.secondaryLabel, maxLines = 1,
                )
            }
        }
    }
}
```

- [ ] **Step 2: Compile (EmulatorActivity's callsite must compile unchanged)**

Run: `JAVA_HOME=$HOME/Android/jdk17 ./gradlew :app:compileDebugJavaWithJavac`
Expected: `BUILD SUCCESSFUL`

- [ ] **Step 3: Commit**

```bash
git add -A Android/app/src/main/java/io/sameboy/android/
git commit -m "feat(android): Cupertino in-game menu - action sheet + slot cards, Host contract preserved"
```

---

### Task 7: Link screen

**Files:**
- Create: `Android/app/src/main/java/io/sameboy/android/LinkUi.kt`
- Modify: `Android/app/src/main/java/io/sameboy/android/LinkActivity.java`

**Interfaces:**
- Consumes: Task 2–3; `R.string.link_host/link_join/link_disconnect/link_hint_join/link_cable`.
- Produces:
  - `object LinkUi` with `@JvmStatic fun bind(activity: ComponentActivity, deviceLine: String, cb: Callbacks): Model`
  - `LinkUi.Model` with `fun setStatus(text: String)`
  - `LinkUi.Callbacks`: `onHost()`, `onJoin(ip: String)`, `onDisconnect()`, `onBack()`

- [ ] **Step 1: Write `LinkUi.kt`**

```kotlin
package io.sameboy.android

import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import io.sameboy.android.cupertino.CupText
import io.sameboy.android.cupertino.Cupertino
import io.sameboy.android.cupertino.CupertinoNavBar
import io.sameboy.android.cupertino.CupertinoSection
import io.sameboy.android.cupertino.CupertinoTheme
import io.sameboy.android.cupertino.NavRow

object LinkUi {
    class Model internal constructor() {
        internal val status = mutableStateOf("Idle")
        fun setStatus(text: String) { status.value = text }
    }

    interface Callbacks {
        fun onHost()
        fun onJoin(ip: String)
        fun onDisconnect()
        fun onBack()
    }

    @JvmStatic
    fun bind(activity: ComponentActivity, deviceLine: String, cb: Callbacks): Model {
        val model = Model()
        activity.setContent { CupertinoTheme { LinkScreen(model, deviceLine, cb) } }
        return model
    }
}

@Composable
private fun LinkScreen(model: LinkUi.Model, deviceLine: String, cb: LinkUi.Callbacks) {
    var peer by remember { mutableStateOf("") }
    val status by model.status
    Column(Modifier.fillMaxSize()) {
        CupertinoNavBar(title = stringResource(R.string.link_cable), onBack = { cb.onBack() })

        CupertinoSection(footer = deviceLine, rows = listOf(
            { NavRow(stringResource(R.string.link_host), onClick = { cb.onHost() }) },
        ))

        CupertinoSection(header = "Join", footer = "Status: $status", rows = listOf(
            {
                Box(Modifier.fillMaxWidth().padding(16.dp)) {
                    if (peer.isEmpty()) {
                        CupText(
                            stringResource(R.string.link_hint_join),
                            Cupertino.type.body, Cupertino.colors.tertiaryLabel,
                        )
                    }
                    BasicTextField(
                        value = peer,
                        onValueChange = { peer = it },
                        textStyle = Cupertino.type.body.copy(color = Cupertino.colors.label),
                        cursorBrush = SolidColor(Cupertino.colors.systemBlue),
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                    )
                }
            },
            {
                NavRow(stringResource(R.string.link_join), onClick = {
                    val ip = peer.trim()
                    if (ip.isNotEmpty()) cb.onJoin(ip)
                })
            },
            { NavRow(stringResource(R.string.link_disconnect), onClick = { cb.onDisconnect() }) },
        ))
    }
}
```

- [ ] **Step 2: Update `LinkActivity.java` onCreate (keep poll/lifecycle/localIp exactly)**

Replace the view-building portion of `onCreate` (everything from `LinearLayout root = ...` through the three `setOnClickListener` calls at the end) and the `status` field with:

Field change: replace `private TextView status;` with `private LinkUi.Model model;`

New `onCreate` body after the two guard clauses (`if (b != null)...` and `if (ctx == 0)...` stay):

```java
        model = LinkUi.bind(this,
            "This device: " + localIp() + "  (port " + PORT + ")",
            new LinkUi.Callbacks() {
                @Override public void onHost() { NativeBridge.nativeLinkListen(ctx, PORT); }
                @Override public void onJoin(String ip) { NativeBridge.nativeLinkConnect(ctx, ip, PORT); }
                @Override public void onDisconnect() { NativeBridge.nativeLinkDisconnect(ctx); }
                @Override public void onBack() { finish(); }
            });
```

And the poll runnable body changes from `status.setText(...)` to:

```java
            if (ctx != 0) {
                int st = NativeBridge.nativeLinkStatus(ctx);
                model.setStatus(names[st >= 0 && st < names.length ? st : 0]);
            }
            handler.postDelayed(this, 500);
```

Remove now-unused imports (`Gravity`, `Button`, `EditText`, `LinearLayout`, `TextView`, `InputType`, `Build`).

- [ ] **Step 3: Compile**

Run: `JAVA_HOME=$HOME/Android/jdk17 ./gradlew :app:compileDebugJavaWithJavac`
Expected: `BUILD SUCCESSFUL`

- [ ] **Step 4: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/LinkUi.kt Android/app/src/main/java/io/sameboy/android/LinkActivity.java
git commit -m "feat(android): Cupertino link-cable screen - grouped host/join sections, live status footer"
```

---

### Task 8: Printer feed screen

**Files:**
- Create: `Android/app/src/main/java/io/sameboy/android/PrinterUi.kt`
- Modify: `Android/app/src/main/java/io/sameboy/android/PrinterFeedActivity.java`

**Interfaces:**
- Consumes: Task 2–3; `R.string.printer_feed/printer_empty/save/share/clear`.
- Produces:
  - `object PrinterUi` with `@JvmStatic fun bind(activity: ComponentActivity, feed: Bitmap?, cb: Callbacks)`
  - `PrinterUi.Callbacks`: `onSave()`, `onShare()`, `onClear()`, `onBack()`

- [ ] **Step 1: Write `PrinterUi.kt`**

```kotlin
package io.sameboy.android

import android.graphics.Bitmap
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import io.sameboy.android.cupertino.ButtonStyle
import io.sameboy.android.cupertino.CupText
import io.sameboy.android.cupertino.Cupertino
import io.sameboy.android.cupertino.CupertinoButton
import io.sameboy.android.cupertino.CupertinoNavBar
import io.sameboy.android.cupertino.CupertinoTheme

object PrinterUi {
    interface Callbacks {
        fun onSave()
        fun onShare()
        fun onClear()
        fun onBack()
    }

    @JvmStatic
    fun bind(activity: ComponentActivity, feed: Bitmap?, cb: Callbacks) {
        activity.setContent { CupertinoTheme { PrinterScreen(feed, cb) } }
    }
}

@Composable
private fun PrinterScreen(feed: Bitmap?, cb: PrinterUi.Callbacks) {
    Column(Modifier.fillMaxSize()) {
        CupertinoNavBar(title = stringResource(R.string.printer_feed), onBack = { cb.onBack() })
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            CupertinoButton(stringResource(R.string.save), ButtonStyle.Filled, enabled = feed != null) { cb.onSave() }
            CupertinoButton(stringResource(R.string.share), enabled = feed != null) { cb.onShare() }
            CupertinoButton(
                stringResource(R.string.clear), ButtonStyle.Plain, destructive = true,
            ) { cb.onClear() }
        }
        if (feed == null) {
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                CupText(
                    stringResource(R.string.printer_empty),
                    Cupertino.type.subheadline, Cupertino.colors.secondaryLabel,
                )
            }
        } else {
            Column(
                Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Image(
                    bitmap = feed.asImageBitmap(),
                    contentDescription = null,
                    contentScale = ContentScale.FillWidth,
                    filterQuality = FilterQuality.None,   // crisp GB pixels
                    modifier = Modifier.fillMaxWidth()
                        .clip(RoundedCornerShape(12.dp))
                        .background(Cupertino.colors.secondarySystemGroupedBackground),
                )
            }
        }
    }
}
```

- [ ] **Step 2: Update `PrinterFeedActivity.java` onCreate**

Replace everything in `onCreate` after `bitmap = buildBitmap();` (the whole view-building block, `setContentView`, and the click listeners) with:

```java
        PrinterUi.bind(this, bitmap, new PrinterUi.Callbacks() {
            @Override public void onSave() { saveToPictures(); }
            @Override public void onShare() { sharePng(); }
            @Override public void onClear() { NativeBridge.nativePrinterClear(ctx); finish(); }
            @Override public void onBack() { finish(); }
        });
```

Remove now-unused imports (`Color`, `Gravity`, `View`, `Button`, `ImageView`, `LinearLayout`, `ScrollView`, `TextView`). Keep `saveToPictures()`, `sharePng()`, `buildBitmap()`, permission handling untouched.

- [ ] **Step 3: Compile**

Run: `JAVA_HOME=$HOME/Android/jdk17 ./gradlew :app:compileDebugJavaWithJavac`
Expected: `BUILD SUCCESSFUL`

- [ ] **Step 4: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/PrinterUi.kt Android/app/src/main/java/io/sameboy/android/PrinterFeedActivity.java
git commit -m "feat(android): Cupertino printer-feed screen - feed card, filled/tinted/destructive buttons"
```

---

### Task 9: Gamepad remap screen

**Files:**
- Create: `Android/app/src/main/java/io/sameboy/android/RemapUi.kt`
- Modify: `Android/app/src/main/java/io/sameboy/android/GamepadRemapActivity.java`

**Interfaces:**
- Consumes: Task 2–3; Java `GamepadMapper` (`KEYS`, `GB_NAMES[]`, `keycodeFor(i)`); `R.string.gamepad_buttons/reset_defaults`.
- Produces:
  - `object RemapUi` with `@JvmStatic fun bind(activity: ComponentActivity, cb: Callbacks): Model`
  - `RemapUi.Model` with `fun update(capturing: Int, bindingNames: List<String>)`
  - `RemapUi.Callbacks`: `onArm(index: Int)`, `onReset()`, `onBack()`

- [ ] **Step 1: Write `RemapUi.kt`**

```kotlin
package io.sameboy.android

import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import io.sameboy.android.cupertino.CupertinoNavBar
import io.sameboy.android.cupertino.CupertinoSection
import io.sameboy.android.cupertino.CupertinoTheme
import io.sameboy.android.cupertino.NavRow

object RemapUi {
    class Model internal constructor() {
        internal val capturing = mutableIntStateOf(-1)
        internal val names = mutableStateListOf<String>()
        fun update(capturing: Int, bindingNames: List<String>) {
            this.capturing.intValue = capturing
            names.clear(); names.addAll(bindingNames)
        }
    }

    interface Callbacks {
        fun onArm(index: Int)
        fun onReset()
        fun onBack()
    }

    @JvmStatic
    fun bind(activity: ComponentActivity, cb: Callbacks): Model {
        val model = Model()
        activity.setContent { CupertinoTheme { RemapScreen(model, cb) } }
        return model
    }
}

@Composable
private fun RemapScreen(model: RemapUi.Model, cb: RemapUi.Callbacks) {
    val capturing by model.capturing
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        CupertinoNavBar(title = stringResource(R.string.gamepad_buttons), onBack = { cb.onBack() })
        CupertinoSection(
            footer = "Tap an input, then press a controller button.",
            rows = model.names.mapIndexed { i, name ->
                @Composable {
                    NavRow(
                        GamepadMapper.GB_NAMES[i],
                        value = if (capturing == i) "press a button\u2026" else name,
                        onClick = { cb.onArm(i) },
                    )
                }
            },
        )
        CupertinoSection(rows = listOf(
            { NavRow(stringResource(R.string.reset_defaults), onClick = { cb.onReset() }) },
        ))
    }
}
```

- [ ] **Step 2: Rewrite `GamepadRemapActivity.java` (keep `dispatchKeyEvent` capture)**

Full new content:

```java
package io.sameboy.android;

import androidx.appcompat.app.AppCompatActivity;
import android.os.Bundle;
import android.view.KeyEvent;

import java.util.ArrayList;
import java.util.List;

/** Bind each GB input to a controller button: tap a row to arm, then press a button. */
public class GamepadRemapActivity extends AppCompatActivity {
    private GamepadMapper pad;
    private int capturing = -1;                 // GB key awaiting a keycode, or -1
    private RemapUi.Model model;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        pad = new GamepadMapper(this);
        model = RemapUi.bind(this, new RemapUi.Callbacks() {
            @Override public void onArm(int index) { capturing = index; push(); }
            @Override public void onReset() { pad.resetDefaults(); capturing = -1; push(); }
            @Override public void onBack() { finish(); }
        });
        push();
    }

    private void push() {
        List<String> names = new ArrayList<>();
        for (int i = 0; i < GamepadMapper.KEYS; i++)
            names.add(KeyEvent.keyCodeToString(pad.keycodeFor(i)));
        model.update(capturing, names);
    }

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if (capturing >= 0 && event.getAction() == KeyEvent.ACTION_DOWN
                && GamepadMapper.isGamepadKeycode(event.getKeyCode())) {
            pad.setBinding(capturing, event.getKeyCode());
            capturing = -1;
            push();
            return true;
        }
        return super.dispatchKeyEvent(event);
    }
}
```

- [ ] **Step 3: Compile**

Run: `JAVA_HOME=$HOME/Android/jdk17 ./gradlew :app:compileDebugJavaWithJavac`
Expected: `BUILD SUCCESSFUL`

- [ ] **Step 4: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/RemapUi.kt Android/app/src/main/java/io/sameboy/android/GamepadRemapActivity.java
git commit -m "feat(android): Cupertino gamepad remap screen - grouped binding rows, armed row indicator"
```

---

### Task 10: Full build

**Files:** none (build verification)

- [ ] **Step 1: Full assembleDebug (all 4 ABIs, NDK, packaging)**

Run: `JAVA_HOME=$HOME/Android/jdk17 ./gradlew assembleDebug`
Expected: `BUILD SUCCESSFUL`; APK at `app/build/outputs/apk/debug/app-debug.apk`.

- [ ] **Step 2: Release build sanity (R8 with Compose)**

Run: `JAVA_HOME=$HOME/Android/jdk17 ./gradlew assembleRelease`
Expected: `BUILD SUCCESSFUL` (Compose ships consumer proguard rules; name-based JNI keeps are already in proguard-rules.pro).

- [ ] **Step 3: Commit any needed fixes; otherwise nothing to commit**

---

### Task 11: On-device verification

**Files:** none (verification; screenshots to `/tmp/sameboy-ui/`)

Device: Samsung SM-T505 tablet, `adb -s R9TR20NR6YJ` (fall back to Waydroid `192.168.240.112:5555` if the tablet is unreachable).

- [ ] **Step 1: Install**

```bash
adb -s R9TR20NR6YJ install -r app/build/outputs/apk/debug/app-debug.apk
```
Expected: `Success`

- [ ] **Step 2: Walk every screen, screenshot light + dark**

For each screen (Library, Settings incl. a picker sheet open + palette editor, in-game menu + save slots, Link, Printer feed, Gamepad remap):

```bash
adb -s R9TR20NR6YJ exec-out screencap -p > /tmp/sameboy-ui/<screen>-<mode>.png
```

Toggle mode via Settings → Appearance → Theme (Light, then Dark) — this also exercises `applyTheme()` + activity recreation.

- [ ] **Step 3: Feature-parity smoke**

- Import Folder → count toast appears, grid populates.
- Open ROM → game launches.
- Long-press tile → action sheet: Play / Favorite (★ appears, sorts first) / Remove.
- In-game menu → save to slot 1, load from slot 1; thumbnail shows.
- Settings: change volume + model, relaunch app, values persisted.
- Gamepad remap: arm a row ("press a button…" shows), bind with a controller if attached, Reset to defaults restores.
- Rotate/background-foreground the Library — no crash, state intact.

- [ ] **Step 4: Review screenshots with the read tool; fix visual defects; commit fixes**

---

## Self-Review Notes

- Spec coverage: build changes (T1), Theme (T2), Components incl. every listed widget (T3), Library (T4), Settings + palette editor (T5), game menu + slots (T6), Link (T7), Printer (T8), Remap (T9), verification (T10–11). Alert = `CupertinoAlertShell`, used by the palette editor.
- All Kotlin bridges take `ComponentActivity`; every touched activity extends `AppCompatActivity` which is one. `@JvmStatic` keeps Java callsites clean.
- `GameMenuDialog.show(Activity, Host)` and Host method set match the Java original symbol-for-symbol; EmulatorActivity is not modified.
- Skipped on purpose (YAGNI): collapsing large-title-on-scroll, sheet slide-in animation, ripple/indication frameworks, previews/tooling, nav library, Material3.
