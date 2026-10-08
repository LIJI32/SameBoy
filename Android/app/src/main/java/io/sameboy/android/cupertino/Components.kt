package io.sameboy.android.cupertino

import android.view.MotionEvent
import android.view.Window
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.unit.sp
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.compose.ui.window.DialogWindowProvider
import io.sameboy.android.GamepadMapper

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
private fun RowShell(onClick: (() -> Unit)?, content: @Composable RowScope.() -> Unit) {
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
                detectHorizontalDragGestures { change, _ -> change.consume(); set(change.position.x) }
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

class SheetAction(
    val label: String,
    val destructive: Boolean = false,
    val dismisses: Boolean = true,
    val onClick: () -> Unit,
)

@Composable
private fun SheetButton(
    label: String, color: Color, weight: FontWeight,
    focusRequester: FocusRequester? = null, onClick: () -> Unit,
) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    var focused by remember { mutableStateOf(false) }   // controller cursor: same look as a press
    Box(
        Modifier
            .fillMaxWidth().height(57.dp)
            .background(if (pressed || focused) Cupertino.colors.fill else Color.Transparent)
            .then(if (focusRequester != null) Modifier.focusRequester(focusRequester) else Modifier)
            .onFocusChanged { focused = it.isFocused }
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
    // Put the controller cursor on the first action as soon as the sheet appears.
    val firstFocus = remember { FocusRequester() }
    LaunchedEffect(Unit) { runCatching { firstFocus.requestFocus() } }
    Column(
        Modifier.widthIn(max = 420.dp).fillMaxWidth()
            .clickable(remember { MutableInteractionSource() }, indication = null) {}
            .padding(8.dp),
    ) {
        Column(
            Modifier.fillMaxWidth().weight(1f, fill = false).clip(RoundedCornerShape(14.dp))
                .background(Cupertino.colors.secondarySystemGroupedBackground)
                .verticalScroll(rememberScrollState()),
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
                    focusRequester = if (i == 0) firstFocus else null,
                ) { a.onClick(); if (a.dismisses) onDismiss() }
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
            SheetButton(cancelLabel, Cupertino.colors.systemBlue, FontWeight.SemiBold, onClick = onDismiss)
        }
    }
}

/** Compose Dialogs are separate windows with their own Window.Callback, so DpadActivity's
 *  hat-axis → DPAD synthesis never sees their input; without this a hat-switch d-pad is
 *  dead inside every picker sheet. Wraps the dialog window's callback for the composition. */
@Composable
private fun HatDpadBridge() {
    val window = (LocalView.current.parent as? DialogWindowProvider)?.window
    DisposableEffect(window) {
        val orig = window?.callback
        if (window == null || orig == null) return@DisposableEffect onDispose {}
        val hat = BooleanArray(4)
        window.callback = object : Window.Callback by orig {
            override fun dispatchGenericMotionEvent(ev: MotionEvent): Boolean =
                GamepadMapper.hatToDpadKeys(ev, hat) { orig.dispatchKeyEvent(it) } || orig.dispatchGenericMotionEvent(ev)
        }
        onDispose { window.callback = orig }
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
        HatDpadBridge()
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
        HatDpadBridge()
        Column(
            Modifier.widthIn(max = 340.dp).clip(RoundedCornerShape(14.dp))
                .background(Cupertino.colors.secondarySystemGroupedBackground),
        ) {
            CupText(
                title, Cupertino.type.headline,
                modifier = Modifier.padding(16.dp).fillMaxWidth(),
            )
            Column(Modifier.weight(1f, fill = false).verticalScroll(rememberScrollState())) { content() }
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
            .background(if (enabled) bg else if (style == ButtonStyle.Plain) Color.Transparent else Cupertino.colors.fill)
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

/** iOS readable-width idiom: center content in a <=640dp column.
 *  Geometric no-op on screens narrower than the cap (phones). */
@Composable
fun ReadableContent(content: @Composable () -> Unit) {
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
        Box(Modifier.widthIn(max = 640.dp)) { content() }
    }
}
