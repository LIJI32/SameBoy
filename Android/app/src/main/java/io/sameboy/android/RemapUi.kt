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
import io.sameboy.android.cupertino.ReadableContent

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
    ReadableContent {
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
}
