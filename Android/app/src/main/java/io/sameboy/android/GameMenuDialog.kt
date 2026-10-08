package io.sameboy.android

import android.app.Activity
import android.graphics.Bitmap
import android.graphics.Color as AColor
import android.graphics.drawable.ColorDrawable
import android.text.format.DateUtils
import android.view.Gravity
import android.view.MotionEvent
import android.view.ViewGroup
import androidx.activity.ComponentDialog
import androidx.compose.foundation.Image
import androidx.compose.foundation.border
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.isSystemInDarkTheme
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
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.ImeAction
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
        fun onSetBorderMode(mode: Int)       // 0 SGB-aware games only, 1 Never, 2 Always
        fun borderMode(): Int
        fun onOpenSettings()
        fun onConnectAccessory(which: Int)   // 0 = None, 1 = Printer
        fun onPrinterFeed()
        fun onLinkCable()
        fun printerConnected(): Boolean
        fun onExitGame()
        fun stateFile(slot: Int): File
        fun thumbnail(slot: Int): Bitmap?
        fun cheats(): List<CheatStore.Cheat>                 // host-owned snapshot, read-only
        fun onAddCheat(code: String, desc: String): Boolean  // false = Core rejected the code
        fun onToggleCheat(index: Int, enabled: Boolean)
        fun onRemoveCheat(index: Int)
    }

    private enum class Screen { Menu, SaveSlots, LoadSlots, Models, Border, Accessory, Cheats, RemoveCheat, AddCheat }

    @JvmStatic
    fun show(a: Activity, h: Host) {
        val chained = booleanArrayOf(false)   // an Activity took over; don't unpause yet
        val hat = BooleanArray(4)
        val dialog = object : ComponentDialog(a) {
            // Hat-switch d-pads never produce DPAD keys; synthesize them so focus traversal works.
            override fun dispatchGenericMotionEvent(ev: MotionEvent): Boolean =
                GamepadMapper.hatToDpadKeys(ev, hat) { dispatchKeyEvent(it) } || super.dispatchGenericMotionEvent(ev)
        }
        dialog.setContentView(ComposeView(a).apply {
            layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT,
            )
            setContent {
                // EmulatorActivity is a plain Activity: AppCompat's night-mode override never
                // reaches its configuration, so resolve the in-app theme by hand.
                val dark = when (Settings(a).themeMode()) { 1 -> false; 2 -> true; else -> isSystemInDarkTheme() }
                CupertinoTheme(fillBackground = false, dark = dark) {
                    // Box (propagateMinConstraints=false) drops the dialog's exact full-width
                    // min constraint so ActionSheetContent's widthIn(max = 420.dp) can apply.
                    Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.BottomCenter) {
                        MenuContent(
                            h,
                            dismiss = { dialog.dismiss() },
                            takeOver = { chained[0] = true; dialog.dismiss() },
                        )
                    }
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
                    SheetAction("Save state", dismisses = false) { screen = Screen.SaveSlots },
                    SheetAction("Load state", dismisses = false) { screen = Screen.LoadSlots },
                    SheetAction("Reset") { h.onResetGame(); dismiss() },
                    SheetAction("Model", dismisses = false) { screen = Screen.Models },
                    SheetAction("Border", dismisses = false) { screen = Screen.Border },
                    SheetAction("Cheats", dismisses = false) { screen = Screen.Cheats },
                    SheetAction("Connect accessory", dismisses = false) { screen = Screen.Accessory },
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
                    SheetAction("Super Game Boy (SGB)") { h.onSwitchModel(NativeBridge.MODEL_SGB); dismiss() },
                    SheetAction("Game Boy Color (CGB)") { h.onSwitchModel(NativeBridge.MODEL_CGB_E); dismiss() },
                    SheetAction("Game Boy Advance (AGB)") { h.onSwitchModel(NativeBridge.MODEL_AGB); dismiss() },
                ),
                cancelLabel = "Cancel",
                onDismiss = dismiss,
            )
            Screen.Border -> {
                val mode = h.borderMode()
                ActionSheetContent(
                    title = "Border",
                    actions = listOf("SGB-aware games only", "Never", "Always").mapIndexed { i, label ->
                        SheetAction((if (i == mode) "\u2713 " else "") + label) {
                            h.onSetBorderMode(i); dismiss()
                        }
                    },
                    cancelLabel = "Cancel",
                    onDismiss = dismiss,
                )
            }
            Screen.Accessory -> {
                val connected = h.printerConnected()
                ActionSheetContent(
                    title = stringResource(R.string.connect_accessory),
                    actions = listOf(
                        SheetAction(
                            (if (!connected) "\u2713 " else "") + stringResource(R.string.accessory_none),
                        ) { h.onConnectAccessory(0); dismiss() },
                        SheetAction(
                            (if (connected) "\u2713 " else "") + stringResource(R.string.accessory_printer),
                        ) { h.onConnectAccessory(1); dismiss() },
                    ),
                    cancelLabel = "Cancel",
                    onDismiss = dismiss,
                )
            }
            Screen.SaveSlots -> SlotSheet(h, forSave = true, dismiss = dismiss)
            Screen.LoadSlots -> SlotSheet(h, forSave = false, dismiss = dismiss)
            Screen.Cheats -> {
                var cheats by remember { mutableStateOf(h.cheats().toList()) }
                val rows = if (cheats.isEmpty()) listOf(SheetAction("No cheats yet", dismisses = false) {})
                else cheats.mapIndexed { i, c ->
                    SheetAction(cheatLabel(c), dismisses = false) {
                        h.onToggleCheat(i, !c.enabled); cheats = h.cheats().toList()
                    }
                }
                ActionSheetContent(
                    title = "Cheats",
                    actions = rows + listOfNotNull(
                        SheetAction("Add cheat\u2026", dismisses = false) { screen = Screen.AddCheat },
                        if (cheats.isEmpty()) null
                        else SheetAction("Remove cheat\u2026", destructive = true, dismisses = false) { screen = Screen.RemoveCheat },
                    ),
                    cancelLabel = "Done",
                    onDismiss = dismiss,
                )
            }
            Screen.RemoveCheat -> ActionSheetContent(
                title = "Remove cheat",
                actions = h.cheats().mapIndexed { i, c ->
                    SheetAction(cheatLabel(c), destructive = true, dismisses = false) {
                        h.onRemoveCheat(i); screen = Screen.Cheats
                    }
                },
                cancelLabel = "Back",
                onDismiss = { screen = Screen.Cheats },
            )
            Screen.AddCheat -> AddCheatCard(h, done = { screen = Screen.Cheats })
        }
    }

    private fun cheatLabel(c: CheatStore.Cheat): String =
        (if (c.enabled) "\u2713 " else "") + c.desc.ifBlank { c.code } + (if (c.desc.isNotBlank()) "  ${c.code}" else "")

    @Composable
    private fun AddCheatCard(h: Host, done: () -> Unit) {
        var code by remember { mutableStateOf("") }
        var name by remember { mutableStateOf("") }
        var error by remember { mutableStateOf<String?>(null) }
        val codeFocus = remember { FocusRequester() }
        val nameFocus = remember { FocusRequester() }
        LaunchedEffect(Unit) { runCatching { codeFocus.requestFocus() } }
        fun add() {
            val c = code.trim().uppercase()
            if (c.isNotEmpty() && h.onAddCheat(c, name.trim())) done()
            else error = "Unrecognized code \u2014 use GameShark 01FF16D0 or Game Genie 000-00A-00B"
        }
        Column(
            Modifier.fillMaxWidth().padding(8.dp).clip(RoundedCornerShape(14.dp))
                .background(Cupertino.colors.secondarySystemGroupedBackground),
        ) {
            Column(Modifier.padding(16.dp)) {
                CupText("Add cheat", Cupertino.type.headline, modifier = Modifier.padding(bottom = 12.dp))
                CheatField(code, { code = it; error = null }, "Code", codeFocus, ImeAction.Next) { nameFocus.requestFocus() }
                Spacer(Modifier.height(8.dp))
                CheatField(name, { name = it }, "Name (optional)", nameFocus, ImeAction.Done, ::add)
                error?.let { CupText(it, Cupertino.type.footnote, Cupertino.colors.systemRed, modifier = Modifier.padding(top = 8.dp)) }
            }
            Box(Modifier.fillMaxWidth().height(0.5.dp).background(Cupertino.colors.separator))
            Row(Modifier.fillMaxWidth()) {
                AlertButton("Cancel", Modifier.weight(1f), onClick = done)
                Box(Modifier.width(0.5.dp).height(44.dp).background(Cupertino.colors.separator))
                AlertButton("Add", Modifier.weight(1f), onClick = ::add)
            }
        }
    }

    @Composable
    private fun CheatField(
        value: String, onChange: (String) -> Unit, hint: String,
        focusRequester: FocusRequester, ime: ImeAction, onAction: () -> Unit,
    ) {
        var focused by remember { mutableStateOf(false) }
        val shape = RoundedCornerShape(10.dp)
        Box(
            Modifier.fillMaxWidth().height(44.dp).clip(shape).background(Cupertino.colors.fill)
                .border(if (focused) 2.dp else 0.dp, if (focused) Cupertino.colors.systemBlue else Color.Transparent, shape)
                .padding(horizontal = 12.dp),
            contentAlignment = Alignment.CenterStart,
        ) {
            if (value.isEmpty()) CupText(hint, Cupertino.type.body, Cupertino.colors.tertiaryLabel, maxLines = 1)
            BasicTextField(
                value = value,
                onValueChange = onChange,
                textStyle = Cupertino.type.body.copy(color = Cupertino.colors.label),
                cursorBrush = SolidColor(Cupertino.colors.systemBlue),
                singleLine = true,
                keyboardOptions = KeyboardOptions(imeAction = ime),
                keyboardActions = KeyboardActions(onNext = { onAction() }, onDone = { onAction() }),
                modifier = Modifier.fillMaxWidth().focusRequester(focusRequester).onFocusChanged { focused = it.isFocused },
            )
        }
    }

    @Composable
    private fun SlotSheet(h: Host, forSave: Boolean, dismiss: () -> Unit) {
        // Controller cursor lands on the first usable slot (load: first non-empty one).
        val firstFocus = remember { FocusRequester() }
        val firstUsable = (0 until SLOTS).firstOrNull { forSave || h.stateFile(it).exists() }
        LaunchedEffect(Unit) { runCatching { firstFocus.requestFocus() } }
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
                            SlotCard(
                                h, slot, forSave, dismiss, Modifier.weight(1f),
                                if (slot == firstUsable) firstFocus else null,
                            )
                        }
                    }
                    Spacer(Modifier.height(8.dp))
                }
            }
            Spacer(Modifier.height(8.dp))
            var cancelFocused by remember { mutableStateOf(false) }
            Column(
                Modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp))
                    .background(if (cancelFocused) Cupertino.colors.fill else Cupertino.colors.secondarySystemGroupedBackground)
                    .onFocusChanged { cancelFocused = it.isFocused }
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
    private fun SlotCard(
        h: Host, slot: Int, forSave: Boolean, dismiss: () -> Unit, modifier: Modifier,
        focusRequester: FocusRequester?,
    ) {
        val file = h.stateFile(slot)
        val exists = file.exists()
        val enabled = forSave || exists
        val bmp = if (exists) h.thumbnail(slot) else null
        var focused by remember { mutableStateOf(false) }
        val shape = RoundedCornerShape(10.dp)
        Row(
            modifier
                .clip(shape)
                .background(Cupertino.colors.fill)
                .border(if (focused) 3.dp else 0.dp, if (focused) Cupertino.colors.systemBlue else Color.Transparent, shape)
                .then(if (focusRequester != null) Modifier.focusRequester(focusRequester) else Modifier)
                .onFocusChanged { focused = it.isFocused }
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

/** Alert-card footer button (CupertinoAlertShell look) with controller focus highlight.
 *  Shared with ResumePrompt, which lives in its own dialog window. */
@Composable
internal fun AlertButton(
    label: String, modifier: Modifier,
    focusRequester: FocusRequester? = null, bold: Boolean = false, onClick: () -> Unit,
) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    var focused by remember { mutableStateOf(false) }
    Box(
        modifier.height(44.dp)
            .background(if (pressed || focused) Cupertino.colors.fill else Color.Transparent)
            .then(if (focusRequester != null) Modifier.focusRequester(focusRequester) else Modifier)
            .onFocusChanged { focused = it.isFocused }
            .clickable(interaction, indication = null, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        CupText(
            label, if (bold) Cupertino.type.headline else Cupertino.type.body,
            Cupertino.colors.systemBlue, maxLines = 1,
        )
    }
}
