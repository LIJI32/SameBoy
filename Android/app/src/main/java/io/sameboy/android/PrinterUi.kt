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
import io.sameboy.android.cupertino.ReadableContent

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
    ReadableContent {
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
}
