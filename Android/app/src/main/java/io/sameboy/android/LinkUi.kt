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
import io.sameboy.android.cupertino.ReadableContent

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
    ReadableContent {
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
}
