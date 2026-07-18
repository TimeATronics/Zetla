package com.zetla.ui.screens.chat

import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember


@Composable
fun ListAutoScrollToBottom(
    threshold: Int = 2,
    listState: LazyListState,
    isStreaming: Boolean
) {
    val itemsCount by remember {
        derivedStateOf { listState.layoutInfo.totalItemsCount }
    }

    val shouldAutoScroll by remember {
        derivedStateOf {
            if (itemsCount <= 0) return@derivedStateOf false
            val lastVisible = listState.layoutInfo.visibleItemsInfo.lastOrNull()?.index ?: 0
            lastVisible >= itemsCount - 1 - threshold
        }
    }

    LaunchedEffect(itemsCount) {
        if (itemsCount > 0) {
            listState.scrollToItem(itemsCount - 1)
        }
    }

    LaunchedEffect(isStreaming, shouldAutoScroll) {
        if (isStreaming && shouldAutoScroll) {
            listState.scrollToItem(itemsCount - 1)
        }
    }
}
