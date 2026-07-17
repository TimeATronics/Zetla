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
    contentVersion: Int = 0
) {
    val itemsCount by remember {
        derivedStateOf { listState.layoutInfo.totalItemsCount }
    }

    LaunchedEffect(itemsCount, contentVersion) {
        if (itemsCount > 0 && !listState.isScrollInProgress) {
            val lastVisible = listState.layoutInfo.visibleItemsInfo.lastOrNull()?.index ?: 0
            val nearBottom = lastVisible >= itemsCount - 1 - threshold
            if (nearBottom) {
                listState.animateScrollToItem(itemsCount - 1)
            }
        }
    }
}
