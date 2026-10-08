package io.sameboy.android

import android.app.Activity
import android.graphics.Color as AColor
import android.graphics.drawable.ColorDrawable
import android.view.MotionEvent
import android.view.ViewGroup
import androidx.activity.ComponentDialog
import androidx.activity.OnBackPressedCallback
import androidx.compose.foundation.background
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.unit.dp
import io.sameboy.android.cupertino.CupText
import io.sameboy.android.cupertino.Cupertino
import io.sameboy.android.cupertino.CupertinoTheme

/** "Continue where you left off?" alert shown at launch when an auto-save exists.
 *  Non-cancelable: exactly one of the two callbacks runs, on the main thread, after dismiss. */
object ResumePrompt {
    @JvmStatic
    fun show(a: Activity, onContinue: Runnable, onStartFresh: Runnable) {
        val hat = BooleanArray(4)
        val dialog = object : ComponentDialog(a) {
            override fun dispatchGenericMotionEvent(ev: MotionEvent): Boolean =
                GamepadMapper.hatToDpadKeys(ev, hat) { dispatchKeyEvent(it) } || super.dispatchGenericMotionEvent(ev)
        }
        dialog.setCancelable(false)
        dialog.setCanceledOnTouchOutside(false)
        // Back must not pick an answer for the user; swallow it.
        dialog.onBackPressedDispatcher.addCallback(dialog, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {}
        })
        var chosen: Runnable? = null
        val pick = { r: Runnable -> chosen = r; dialog.dismiss() }
        dialog.setContentView(ComposeView(a).apply {
            layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT,
            )
            setContent {
                val dark = when (Settings(a).themeMode()) { 1 -> false; 2 -> true; else -> isSystemInDarkTheme() }
                CupertinoTheme(fillBackground = false, dark = dark) {
                    Box(Modifier.fillMaxWidth().padding(24.dp), contentAlignment = Alignment.Center) {
                        val continueFocus = remember { FocusRequester() }
                        LaunchedEffect(Unit) { runCatching { continueFocus.requestFocus() } }
                        Column(
                            Modifier.widthIn(max = 340.dp).clip(RoundedCornerShape(14.dp))
                                .background(Cupertino.colors.secondarySystemGroupedBackground),
                        ) {
                            CupText(
                                "Continue where you left off?", Cupertino.type.headline,
                                modifier = Modifier.padding(16.dp).fillMaxWidth(),
                            )
                            CupText(
                                "An auto-save from your last session was found.", Cupertino.type.body,
                                Cupertino.colors.secondaryLabel,
                                modifier = Modifier.padding(horizontal = 16.dp).padding(bottom = 16.dp).fillMaxWidth(),
                            )
                            Box(Modifier.fillMaxWidth().height(0.5.dp).background(Cupertino.colors.separator))
                            Row(Modifier.fillMaxWidth()) {
                                AlertButton("Start fresh", Modifier.weight(1f)) { pick(onStartFresh) }
                                Box(Modifier.width(0.5.dp).height(44.dp).background(Cupertino.colors.separator))
                                AlertButton("Continue", Modifier.weight(1f), continueFocus, bold = true) { pick(onContinue) }
                            }
                        }
                    }
                }
            }
        })
        dialog.window?.apply {
            setBackgroundDrawable(ColorDrawable(AColor.TRANSPARENT))
            setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
        }
        dialog.setOnDismissListener { chosen?.run() }
        dialog.show()
    }
}
