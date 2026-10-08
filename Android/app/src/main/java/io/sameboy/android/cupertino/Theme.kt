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

/** `dark` defaults to the host Context's uiMode. Plain (non-AppCompat) activities don't get
 *  the in-app theme override applied to their configuration, so they pass it explicitly. */
@Composable
fun CupertinoTheme(fillBackground: Boolean = true, dark: Boolean = isSystemInDarkTheme(), content: @Composable () -> Unit) {
    val colors = if (dark) Dark else Light
    CompositionLocalProvider(LocalCupertinoColors provides colors) {
        if (fillBackground) {
            Box(Modifier.fillMaxSize().background(colors.systemGroupedBackground)) {
                content()
            }
        } else {
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
