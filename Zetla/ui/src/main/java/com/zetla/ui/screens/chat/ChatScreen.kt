package com.zetla.ui.screens.chat

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import android.util.Log
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Chat
import androidx.compose.material.icons.filled.Menu
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.DrawerValue
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalDrawerSheet
import androidx.compose.material3.ModalNavigationDrawer
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberDrawerState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.repeatOnLifecycle
import com.zetla.domain.model.ChatMessage
import com.zetla.domain.model.FileAttachment
import com.zetla.domain.model.FileType
import com.zetla.domain.model.Role
import com.zetla.ui.screens.chat.components.AttachmentListModal
import com.zetla.ui.screens.chat.components.ChatInputTextField
import com.zetla.ui.screens.chat.components.ChatOptions
import com.zetla.ui.screens.chat.components.ConversationList
import com.zetla.ui.screens.chat.components.DeleteConversationDialog
import com.zetla.ui.screens.chat.components.DotsTyping
import com.zetla.ui.screens.chat.components.MessageBubble
import com.zetla.ui.screens.chat.components.ProviderSetupDialog
import com.zetla.ui.screens.chat.components.RenameConversationDialog
import com.zetla.ui.screens.components.ModelSelectionSheet
import kotlinx.coroutines.launch

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ChatScreen(viewModel: ChatViewModel, onNavigateToSettings: () -> Unit, onNavigateToVoiceChat: () -> Unit = {}) {
    val uiState by viewModel.uiState.collectAsStateWithLifecycle()
    val drawerState = rememberDrawerState(DrawerValue.Closed)
    val coroutineScope = rememberCoroutineScope()
    var showRenameDialog by remember { mutableStateOf(false) }
    var showDeleteDialog by remember { mutableStateOf(false) }
    var showModelSheet by remember { mutableStateOf(false) }
    var showAttachmentList by remember { mutableStateOf(false) }

    if (uiState.showOnboarding) {
        ProviderSetupDialog(
            providers = uiState.availableProviders,
            onDismiss = { viewModel.onUiEvent(ChatUiEvent.OnDismissOnboarding) },
            onSave = { providerId, apiKey ->
                viewModel.onUiEvent(ChatUiEvent.OnSetupProvider(providerId, apiKey))
            }
        )
    }

    LaunchedEffect(Unit) {
        if (uiState.isProviderConfigured && uiState.models.isEmpty()) {
            viewModel.onUiEvent(ChatUiEvent.OnFetchModels)
        }
    }

    val lifecycleOwner = LocalLifecycleOwner.current
    LaunchedEffect(lifecycleOwner) {
        lifecycleOwner.lifecycle.repeatOnLifecycle(Lifecycle.State.RESUMED) {
            viewModel.onUiEvent(ChatUiEvent.OnRefreshConfig)
        }
    }

    val selectedConversation = uiState.selectedConversation
    val context = LocalContext.current

    val snackbarHostState = remember { SnackbarHostState() }

    val filePickerLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        uri?.let { selectedUri ->
            val fileAttachment = copyUriToCache(context, selectedUri)
            if (fileAttachment != null) {
                viewModel.onUiEvent(ChatUiEvent.OnAttachFile(fileAttachment))
            } else {
                coroutineScope.launch {
                    snackbarHostState.showSnackbar("Failed to attach file")
                }
            }
        }
    }

    val onAttachFile = {
        val mimeTypes = arrayOf(
            "text/*", "application/pdf", "image/*",
            "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
            "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
            "application/vnd.openxmlformats-officedocument.presentationml.presentation",
            "text/csv", "text/markdown"
        )
        filePickerLauncher.launch(mimeTypes)
    }

    ModalNavigationDrawer(
        drawerState = drawerState,
        drawerContent = {
            ModalDrawerSheet(
                modifier = Modifier.fillMaxWidth(0.8f),
                drawerContainerColor = MaterialTheme.colorScheme.surface
            ) {
                Column(
                    modifier = Modifier
                        .systemBarsPadding()
                        .padding(top = 16.dp)
                ) {
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(16.dp)
                            .clickable {
                                viewModel.onUiEvent(ChatUiEvent.OnNewChat)
                                coroutineScope.launch { drawerState.close() }
                            },
                        shape = RoundedCornerShape(12.dp),
                        color = MaterialTheme.colorScheme.primaryContainer
                    ) {
                        Row(
                            modifier = Modifier.padding(16.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Icon(Icons.AutoMirrored.Filled.Chat, contentDescription = null)
                            Spacer(Modifier.width(12.dp))
                            Text("New Chat", style = MaterialTheme.typography.bodyLarge)
                        }
                    }

                    ConversationList(
                        conversations = uiState.conversations,
                        modifier = Modifier.weight(1f),
                        selectedConversation = selectedConversation,
                        onConversationSelected = { conversation ->
                            viewModel.onUiEvent(ChatUiEvent.OnConversationSelected(conversation))
                            coroutineScope.launch { drawerState.close() }
                        },
                        conversationFilter = uiState.conversationFilter,
                        onConversationFilterSelected = { filter ->
                            viewModel.onUiEvent(ChatUiEvent.OnConversationFilterSelected(filter))
                        }
                    )

                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable {
                                coroutineScope.launch {
                                    drawerState.close()
                                    onNavigateToSettings()
                                }
                            },
                        color = MaterialTheme.colorScheme.surface
                    ) {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(horizontal = 24.dp, vertical = 24.dp),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text("Settings", style = MaterialTheme.typography.titleMedium)
                            Icon(Icons.Default.Settings, contentDescription = "Settings")
                        }
                    }
                }
            }
        }
    ) {
        Scaffold(
            modifier = Modifier.fillMaxSize(),
            snackbarHost = { SnackbarHost(snackbarHostState) },
            topBar = {
                TopAppBar(
                    title = {
                        Text(
                            text = selectedConversation?.title ?: "Zetla Chat",
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    },
                    navigationIcon = {
                        IconButton(onClick = { coroutineScope.launch { drawerState.open() } }) {
                            Icon(Icons.Default.Menu, contentDescription = "Menu")
                        }
                    },
                    actions = {
                        selectedConversation?.let { conv ->
                            ChatOptions(
                                conversation = conv,
                                onClickDelete = { showDeleteDialog = true },
                                onClickRename = { showRenameDialog = true },
                                onClickStar = { isStarred ->
                                    viewModel.onUiEvent(
                                        ChatUiEvent.OnUpdateConversation(conv.copy(isStarred = isStarred))
                                    )
                                }
                            )
                        }
                    },
                    colors = TopAppBarDefaults.topAppBarColors(
                        containerColor = MaterialTheme.colorScheme.surface
                    )
                )
            },
            bottomBar = {
                ChatInputTextField(
                    modifier = Modifier
                        .navigationBarsPadding()
                        .imePadding(),
                    value = uiState.inputText,
                    onValueChange = { viewModel.onUiEvent(ChatUiEvent.OnInputTextEdit(it)) },
                    onSend = { viewModel.onUiEvent(ChatUiEvent.OnMessageSend) },
                    onWebSearch = { viewModel.onUiEvent(ChatUiEvent.OnWebSearchTapped) },
                    isWebSearchEnabled = uiState.isWebSearchEnabled,
                    isCodingEnabled = uiState.isCodingEnabled,
                    onCodingToggle = { viewModel.onUiEvent(ChatUiEvent.OnCodingToggled) },
                    isLoadingOrStreamingResponse = uiState.isLoadingResponse || uiState.isStreamingResponse,
                    onStopRequest = { viewModel.onUiEvent(ChatUiEvent.OnStopRequest) },
                    selectedModel = uiState.selectedModel,
                    onModelClick = { showModelSheet = true },
                    onNavigateToVoiceChat = onNavigateToVoiceChat,
                    attachedFiles = uiState.attachedFiles,
                    onAttachFile = onAttachFile,
                    onRemoveFile = { fileId -> viewModel.onUiEvent(ChatUiEvent.OnRemoveAttachedFile(fileId)) },
                    onShowAttachments = { showAttachmentList = true }
                )
            }
        ) { innerPadding ->
            Box(modifier = Modifier.fillMaxSize().padding(innerPadding)) {
                if (uiState.isInitialized && !uiState.isProviderConfigured) {
                    Box(modifier = Modifier.fillMaxSize()) {
                        if (uiState.messages.isEmpty() && !uiState.isLoadingResponse) {
                            Box(
                                modifier = Modifier.fillMaxSize(),
                                contentAlignment = Alignment.Center
                            ) {
                                Text(
                                    text = "How can I assist you today?",
                                    style = MaterialTheme.typography.titleLarge,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    fontStyle = FontStyle.Italic,
                                    textAlign = TextAlign.Center,
                                    modifier = Modifier.padding(32.dp)
                                )
                            }
                        }
                        Box(
                            modifier = Modifier
                                .matchParentSize()
                                .alpha(0.4f)
                                .background(MaterialTheme.colorScheme.surface)
                                .clickable(onClick = onNavigateToSettings),
                            contentAlignment = Alignment.BottomCenter
                        ) {
                            Surface(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(16.dp),
                                shape = RoundedCornerShape(12.dp),
                                color = MaterialTheme.colorScheme.secondaryContainer,
                                tonalElevation = 4.dp
                            ) {
                                Text(
                                    text = "No provider configured. Tap here to open Settings.",
                                    modifier = Modifier.padding(16.dp),
                                    style = MaterialTheme.typography.bodyMedium,
                                    color = MaterialTheme.colorScheme.onSecondaryContainer,
                                    textAlign = TextAlign.Center
                                )
                            }
                        }
                    }
                } else if (uiState.messages.isEmpty() && !uiState.isLoadingResponse) {
                    Box(
                        modifier = Modifier.fillMaxSize(),
                        contentAlignment = Alignment.Center
                    ) {
                        Text(
                            text = "How can I assist you today?",
                            style = MaterialTheme.typography.titleLarge,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            fontStyle = FontStyle.Italic,
                            textAlign = TextAlign.Center,
                            modifier = Modifier.padding(32.dp)
                        )
                    }
                } else {
                    val listState = rememberLazyListState()
                    val streamingVersion = (uiState.streamingThinking?.length ?: 0) +
                        (uiState.streamingResponse?.length ?: 0)
                    ListAutoScrollToBottom(2, listState, streamingVersion)

                    LazyColumn(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(horizontal = 8.dp),
                        state = listState,
                        contentPadding = PaddingValues(bottom = 64.dp)
                    ) {
                        items(
                            items = uiState.messages,
                            key = { it.id }
                        ) { msg ->
                            Spacer(modifier = Modifier.height(12.dp))
                            if (msg.isUser) {
                                MessageBubble(
                                    message = ChatMessage(
                                        content = msg.content ?: "",
                                        role = Role.USER
                                    )
                                )
                            } else {
                                ThinkingBlock(
                                    thinkingText = msg.thinkingText
                                )
                                MessageBubble(
                                    message = ChatMessage(
                                        content = msg.content ?: "",
                                        role = Role.ASSISTANT
                                    )
                                )
                            }
                        }

                        uiState.streamingThinking?.let { thinking ->
                            item {
                                Spacer(modifier = Modifier.height(8.dp))
                                ThinkingBlock(thinkingText = thinking)
                            }
                        }

                        if (uiState.isLoadingResponse) {
                            item {
                                Spacer(modifier = Modifier.height(16.dp))
                                Box(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(start = 16.dp)
                                ) {
                                    DotsTyping()
                                }
                            }
                        }

                        uiState.streamingResponse?.let { content ->
                            item {
                                Spacer(modifier = Modifier.height(8.dp))
                                MessageBubble(
                                    message = ChatMessage(
                                        content = content,
                                        role = Role.ASSISTANT
                                    )
                                )
                            }
                        }
                    }
                }
            }
        }
    }

    ModelSelectionSheet(
        isVisible = showModelSheet,
        models = uiState.models,
        selectedModel = uiState.selectedModel,
        onModelSelected = { model ->
            viewModel.onUiEvent(ChatUiEvent.OnModelSelected(model))
            showModelSheet = false
        },
        isFetchingModels = uiState.isFetchingModels,
        fetchModels = { viewModel.onUiEvent(ChatUiEvent.OnFetchModels) },
        onDismiss = { showModelSheet = false },
        modelsByProvider = uiState.modelsByProvider,
        reasoningEffort = uiState.reasoningEffort,
        onReasoningEffortSelected = { effort ->
            viewModel.onUiEvent(ChatUiEvent.OnReasoningEffortSelected(effort))
        }
    )

    AttachmentListModal(
        isVisible = showAttachmentList,
        files = uiState.attachedFiles,
        onRemoveFile = { fileId -> viewModel.onUiEvent(ChatUiEvent.OnRemoveAttachedFile(fileId)) },
        onImageClick = { path ->
            // ImageViewerDialog is handled inside AttachmentListModal
        },
        onDismiss = { showAttachmentList = false }
    )

    selectedConversation?.let { conv ->
        if (showRenameDialog) {
            RenameConversationDialog(
                initial = conv.title,
                onSave = { newTitle ->
                    viewModel.onUiEvent(
                        ChatUiEvent.OnUpdateConversation(conv.copy(title = newTitle))
                    )
                    showRenameDialog = false
                },
                onDismiss = { showRenameDialog = false }
            )
        }

        if (showDeleteDialog) {
            DeleteConversationDialog(
                onDelete = {
                    viewModel.onUiEvent(ChatUiEvent.OnDeleteConversation(conv))
                    showDeleteDialog = false
                },
                onDismiss = { showDeleteDialog = false }
            )
        }
    }
}

@Composable
private fun ThinkingBlock(
    thinkingText: String?,
    modifier: Modifier = Modifier
) {
    if (thinkingText.isNullOrBlank()) return

    var expanded by remember { mutableStateOf(false) }

    Column(
        modifier = modifier
            .fillMaxWidth()
            .padding(horizontal = 8.dp, vertical = 4.dp)
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .clickable { expanded = !expanded },
            shape = RoundedCornerShape(12.dp),
            color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
        ) {
            Column(modifier = Modifier.padding(10.dp)) {
                Text(
                    text = if (expanded) "Thinking [hide]" else "Thinking [show]",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.primary
                )
                AnimatedVisibility(visible = expanded) {
                    Text(
                        text = thinkingText,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                }
                if (!expanded) {
                    Text(
                        text = thinkingText.take(80) + if (thinkingText.length > 80) "..." else "",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier
                            .padding(top = 2.dp)
                            .alpha(0.7f)
                    )
                }
            }
        }
    }
}

private fun queryCursorField(context: Context, uri: Uri, column: String): String? {
    return try {
        context.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            if (cursor.moveToFirst()) {
                val idx = cursor.getColumnIndex(column)
                if (idx >= 0) cursor.getString(idx) else null
            } else null
        }
    } catch (_: Exception) { null }
}

private fun queryCursorLong(context: Context, uri: Uri, column: String): Long {
    return try {
        context.contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            if (cursor.moveToFirst()) {
                val idx = cursor.getColumnIndex(column)
                if (idx >= 0) cursor.getLong(idx) else 0L
            } else 0L
        } ?: 0L
    } catch (_: Exception) { 0L }
}

private fun copyUriToCache(context: Context, uri: Uri): FileAttachment? {
    return try {
        val fileName = queryCursorField(context, uri, OpenableColumns.DISPLAY_NAME) ?: "file"
        val fileSize = queryCursorLong(context, uri, OpenableColumns.SIZE)

        if (fileSize > 10 * 1024 * 1024) {
            Log.w("ChatScreen", "File too large: $fileSize bytes")
            return null
        }

        val contentResolver = context.contentResolver
        val cacheDir = java.io.File(context.cacheDir, "zetla_attachments")
        cacheDir.mkdirs()
        val destFile = java.io.File(cacheDir, fileName)

        val copied = contentResolver.openInputStream(uri)?.use { input ->
            destFile.outputStream().use { output ->
                input.copyTo(output)
            }
            true
        } ?: false

        if (!copied || !destFile.exists()) {
            Log.w("ChatScreen", "Failed to copy file to cache")
            return null
        }

        val ext = fileName.substringAfterLast('.', "").lowercase()
        val fileType = when (ext) {
            "pdf" -> FileType.PDF
            "png", "jpg", "jpeg", "gif", "webp", "bmp" -> FileType.IMAGE
            "xlsx", "xls", "csv" -> FileType.SPREADSHEET
            "pptx", "ppt" -> FileType.PRESENTATION
            "docx", "doc", "rtf" -> FileType.DOCUMENT
            "txt", "md" -> FileType.TEXT
            else -> FileType.UNKNOWN
        }

        Log.d("ChatScreen", "File attached: $fileName (${fileSize}B, type=$fileType, path=${destFile.absolutePath})")

        FileAttachment(
            id = java.util.UUID.randomUUID().toString(),
            name = fileName,
            path = destFile.absolutePath,
            type = fileType,
            size = fileSize
        )
    } catch (e: Exception) {
        Log.e("ChatScreen", "Failed to copy file", e)
        null
    }
}
