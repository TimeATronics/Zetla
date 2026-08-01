#pragma once
#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeInit(
    JNIEnv* env, jclass cls);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeInitWithPath(
    JNIEnv* env, jclass cls, jstring storage_path);

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeShutdown(
    JNIEnv* env, jclass cls);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeCreateSession(
    JNIEnv* env, jclass cls, jstring model, jstring system_prompt);

JNIEXPORT jboolean JNICALL Java_com_zetla_data_ZetlaCore_nativeSendMessage(
    JNIEnv* env, jclass cls, jstring session_id, jstring message, jobject callback);

JNIEXPORT jboolean JNICALL Java_com_zetla_data_ZetlaCore_nativeSendMessageSse(
    JNIEnv* env, jclass cls, jstring session_id, jstring message, jobject callback);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeDeleteSession(
    JNIEnv* env, jclass cls, jstring session_id);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeGetSessionInfo(
    JNIEnv* env, jclass cls, jstring session_id);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeGetHistory(
    JNIEnv* env, jclass cls, jstring session_id);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeClearHistory(
    JNIEnv* env, jclass cls, jstring session_id);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeSetSessionOptions(
    JNIEnv* env, jclass cls, jstring session_id, jstring options_json);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeGetSessionOptions(
    JNIEnv* env, jclass cls, jstring session_id);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeSetSessionModel(
    JNIEnv* env, jclass cls, jstring session_id, jstring model);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeLoadSession(
    JNIEnv* env, jclass cls, jstring session_id);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeListSessions(
    JNIEnv* env, jclass cls);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeDeleteFromStorage(
    JNIEnv* env, jclass cls, jstring session_id);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeSessionExistsOnDisk(
    JNIEnv* env, jclass cls, jstring session_id);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeCompactSession(
    JNIEnv* env, jclass cls, jstring session_id);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeGetCompactionInfo(
    JNIEnv* env, jclass cls, jstring session_id);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeAddTool(
    JNIEnv* env, jclass cls, jstring session_id, jstring name, jstring description, jstring parameters_schema);

JNIEXPORT jboolean JNICALL Java_com_zetla_data_ZetlaCore_nativeSendMessageAgentic(
    JNIEnv* env, jclass cls, jstring session_id, jstring message, jobject callback);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeVersion(
    JNIEnv* env, jclass cls);

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeSetApiKey(
    JNIEnv* env, jclass cls, jstring api_key);

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeSetModel(
    JNIEnv* env, jclass cls, jstring model);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeListProviders(
    JNIEnv* env, jclass cls);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeSetProvider(
    JNIEnv* env, jclass cls, jstring provider_id);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeListModels(
    JNIEnv* env, jclass cls);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeAddFile(
    JNIEnv* env, jclass cls, jstring session_id, jstring file_path);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeRemoveFile(
    JNIEnv* env, jclass cls, jstring session_id, jstring file_id);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeListFiles(
    JNIEnv* env, jclass cls, jstring session_id);

JNIEXPORT jboolean JNICALL Java_com_zetla_data_ZetlaCore_nativeSendMessageWithFiles(
    JNIEnv* env, jclass cls, jstring session_id, jstring message,
    jobjectArray file_ids, jobject callback);

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeSetSystemPrompt(
    JNIEnv* env, jclass cls, jstring system_prompt);

JNIEXPORT jboolean JNICALL Java_com_zetla_data_ZetlaCore_nativeSetSessionSystemPrompt(
    JNIEnv* env, jclass cls, jstring session_id, jstring system_prompt);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeGetSystemPrompt(
    JNIEnv* env, jclass cls);

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeSetDefaultOptions(
    JNIEnv* env, jclass cls, jstring options_json);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeGetDefaultOptions(
    JNIEnv* env, jclass cls);

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeSetProviderConfig(
    JNIEnv* env, jclass cls, jstring provider_id, jstring api_key, jstring base_url, jboolean enabled);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeGetProviderConfig(
    JNIEnv* env, jclass cls, jstring provider_id);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeListProviderConfigs(
    JNIEnv* env, jclass cls);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeListProvidersModels(
    JNIEnv* env, jclass cls);

JNIEXPORT jboolean JNICALL Java_com_zetla_data_ZetlaCore_nativeSetSessionWebSearch(
    JNIEnv* env, jclass cls, jstring session_id, jboolean enabled);

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeSetSearchProvider(
    JNIEnv* env, jclass cls, jstring provider);

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeSetExaApiKey(
    JNIEnv* env, jclass cls, jstring api_key);

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeSetToolExecutor(
    JNIEnv* env, jclass cls, jstring session_id, jobject callback);

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeCancelRequest(
    JNIEnv* env, jclass cls);

JNIEXPORT jboolean JNICALL Java_com_zetla_data_ZetlaCore_nativeSendMessageWithImages(
    JNIEnv* env, jclass cls, jstring session_id, jstring message,
    jobjectArray image_data_uris, jobject callback);

//  RAG (Hyperbolic Search) 

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeRagInit(
    JNIEnv* env, jclass cls, jstring model_path);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeRagAddFile(
    JNIEnv* env, jclass cls, jstring session_id, jstring file_path, jstring text_content);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeRagSearch(
    JNIEnv* env, jclass cls, jstring session_id, jstring query, jint top_k, jstring scope_file);

JNIEXPORT jint JNICALL Java_com_zetla_data_ZetlaCore_nativeRagChunkCount(
    JNIEnv* env, jclass cls, jstring session_id);

JNIEXPORT jlong JNICALL Java_com_zetla_data_ZetlaCore_nativeRagMemoryBytes(
    JNIEnv* env, jclass cls, jstring session_id);

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeRagRemoveSession(
    JNIEnv* env, jclass cls, jstring session_id);

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeRagSetDebugCallback(
    JNIEnv* env, jclass cls, jobject callback);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeCreateSpace(
    JNIEnv* env, jclass cls, jstring model, jstring system_prompt);

JNIEXPORT jint JNICALL Java_com_zetla_data_ZetlaCore_nativeSetSessionRag(
    JNIEnv* env, jclass cls, jstring session_id, jint enabled);

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeInitRagModel(
    JNIEnv* env, jclass cls, jstring model_dir);

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeSetProjectionEnabled(
    JNIEnv* env, jclass cls, jboolean enabled);

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeSetRagConfig(
    JNIEnv* env, jclass cls, jstring config_json);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeGetRagConfig(
    JNIEnv* env, jclass cls);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeAddSpaceFile(
    JNIEnv* env, jclass cls, jstring session_id, jstring file_path, jstring text_content);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeListSpaceFiles(
    JNIEnv* env, jclass cls, jstring session_id);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeIsSpace(
    JNIEnv* env, jclass cls, jstring session_id);

JNIEXPORT jboolean JNICALL Java_com_zetla_data_ZetlaCore_nativeSaveRagSession(
    JNIEnv* env, jclass cls, jstring session_id, jstring dir_path);

JNIEXPORT jboolean JNICALL Java_com_zetla_data_ZetlaCore_nativeLoadRagSession(
    JNIEnv* env, jclass cls, jstring session_id, jstring dir_path);

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeExtractFileText(
    JNIEnv* env, jclass cls, jstring file_path);

#ifdef __cplusplus
}
#endif
