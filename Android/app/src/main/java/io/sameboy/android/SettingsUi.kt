@file:Suppress("EXPOSED_PARAMETER_TYPE") // Settings is package-private Java; same-package Kotlin may use it

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
import io.sameboy.android.cupertino.ReadableContent
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
    NativeBridge.MODEL_DMG_B, NativeBridge.MODEL_CGB_E, NativeBridge.MODEL_AGB, NativeBridge.MODEL_SGB,
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
    var console by remember { mutableIntStateOf(s.consoleTheme()) }
    var swipeDpad by remember { mutableStateOf(s.swipeDpad()) }
    var filter by remember { mutableIntStateOf(s.filter()) }
    var onscreen by remember { mutableIntStateOf(s.onscreenControls()) }

    ReadableContent {
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        CupertinoNavBar(title = stringResource(R.string.settings), onBack = onBack)

        CupertinoSection(header = "Emulation", rows = listOf(
            {
                PickerRow(
                    "Model (next launch)",
                    listOf("Game Boy (DMG)", "Game Boy Color (CGB)", "Game Boy Advance (AGB)",
                           "Super Game Boy (SGB)"),
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

        CupertinoSection(
            header = "Video",
            // ponytail: section footer is the existing Cupertino footnote affordance; no new component.
            footer = if (MODELS[model] != NativeBridge.MODEL_SGB) {
                "\u201CSGB-aware games only\u201D requires the Super Game Boy (SGB) model " +
                    "\u2014 set Model above."
            } else null,
            rows = listOf(
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
                PickerRow(
                    "Border",
                    listOf("SGB-aware games only", "Never", "Always"),
                    border,
                ) { border = it; s.setBorderMode(it) }
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
            {
                PickerRow("Screen filter", listOf("Off", "LCD"), filter) {
                    filter = it; s.setFilter(it)
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
            { ToggleRow("Swipe d-pad", swipeDpad) { swipeDpad = it; s.setSwipeDpad(it) } },
            {
                PickerRow("On-screen controls", listOf("Auto", "Always", "Never"), onscreen) {
                    onscreen = it; s.setOnscreenControls(it)
                }
            },
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
            {
                PickerRow("Console", listOf("SameBoy", "SameBoy Dark", "Follow theme"), console) {
                    console = it; s.setConsoleTheme(it)
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
