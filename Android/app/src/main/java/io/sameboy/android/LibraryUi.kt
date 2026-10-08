@file:Suppress("EXPOSED_PARAMETER_TYPE") // LibraryEntry is package-private Java; same-package Kotlin may use it

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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.border
import androidx.compose.foundation.lazy.grid.itemsIndexed
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onKeyEvent
import androidx.compose.ui.input.key.type
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
        fun setGames(list: List<LibraryEntry>) {
            games.clear()
            // fresh instances: in-place Java field mutations (favorite/lastPlayed) are invisible
            // to snapshot state, so the grid must receive new references to recompose tiles
            list.mapTo(games) { LibraryEntry(it.uri, it.zipEntry, it.displayName, it.title, it.crc32, it.favorite, it.lastPlayed) }
        }
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
    val firstFocus = remember { FocusRequester() }
    // Give a controller user an immediate on-screen selection once the grid has content.
    LaunchedEffect(model.games.isNotEmpty()) {
        if (model.games.isNotEmpty()) runCatching { firstFocus.requestFocus() }
    }
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
                itemsIndexed(model.games, key = { _, it -> it.crc32 }) { index, e ->
                    GameTile(
                        e,
                        focusRequester = if (index == 0) firstFocus else null,
                        onClick = { cb.onPlay(e) },
                        onLongClick = { context = e },
                    )
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
private fun GameTile(
    e: LibraryEntry,
    focusRequester: FocusRequester?,
    onClick: () -> Unit,
    onLongClick: () -> Unit,
) {
    var focused by remember { mutableStateOf(false) }
    val scale by animateFloatAsState(if (focused) 1.06f else 1f, label = "tileScale")
    val shape = RoundedCornerShape(12.dp)
    Column(
        Modifier
            .graphicsLayer { scaleX = scale; scaleY = scale }
            .then(if (focusRequester != null) Modifier.focusRequester(focusRequester) else Modifier)
            .onFocusChanged { focused = it.isFocused }
            // Menu / Y button opens the context menu (long-press equivalent) for a controller.
            .onKeyEvent { ev ->
                if (ev.type == KeyEventType.KeyDown && (ev.key == Key.Menu || ev.key == Key.ButtonY)) {
                    onLongClick(); true
                } else false
            }
            .clip(shape)
            .background(Cupertino.colors.secondarySystemGroupedBackground)
            .border(
                width = if (focused) 3.dp else 0.dp,
                color = if (focused) Cupertino.colors.systemBlue else Color.Transparent,
                shape = shape,
            )
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
