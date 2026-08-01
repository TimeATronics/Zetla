#include "jni_bridge.hpp"
#include "dll_api.h"
#include "../rag/zetla_rag.h"
#include "nlohmann/json.hpp"
#include <mutex>
#include <vector>

static JavaVM* g_jvm = nullptr;
static jmethodID g_on_token = nullptr;
static jmethodID g_on_finished = nullptr;
static jmethodID g_on_sse_data = nullptr;
static jmethodID g_on_sse_done = nullptr;
static jmethodID g_on_agentic_event = nullptr;
static jobject g_callback_obj = nullptr;
static jobject g_sse_callback_obj = nullptr;
static jobject g_agentic_callback_obj = nullptr;
static jclass g_callback_class_global = nullptr;
static jclass g_sse_callback_class_global = nullptr;
static jclass g_agentic_callback_class_global = nullptr;

// Tool executor callback (Java-side)
static jobject g_tool_executor_obj = nullptr;
static jclass g_tool_executor_class_global = nullptr;
static jmethodID g_tool_executor_execute = nullptr;

static std::mutex g_jni_mutex;
static bool g_initialized = false;

static std::string jstring_to_string(JNIEnv* env, jstring jstr) {
    if (!jstr) return {};
    const char* chars = env->GetStringUTFChars(jstr, nullptr);
    std::string result(chars);
    env->ReleaseStringUTFChars(jstr, chars);
    return result;
}

static void init_jvm(JNIEnv* env) {
    if (g_jvm) return;
    std::lock_guard<std::mutex> lock(g_jni_mutex);
    if (g_jvm) return;
    env->GetJavaVM(&g_jvm);
}

static JNIEnv* get_env() {
    if (!g_jvm) return nullptr;
    JNIEnv* env = nullptr;
    jint ret = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (ret == JNI_EDETACHED) {
        ret = g_jvm->AttachCurrentThread(&env, nullptr);
        if (ret != JNI_OK) return nullptr;
    }
    return env;
}

static void jni_stream_callback(const char* json_chunk, int is_finished) {
    JNIEnv* env = get_env();
    if (!env || !g_callback_obj) return;

    jstring jjson = env->NewStringUTF(json_chunk);

    if (is_finished) {
        env->CallVoidMethod(g_callback_obj, g_on_token, jjson);
        env->CallVoidMethod(g_callback_obj, g_on_finished);
    } else {
        env->CallVoidMethod(g_callback_obj, g_on_token, jjson);
    }

    env->DeleteLocalRef(jjson);
}

static void jni_sse_callback(const char* json_data, int is_done) {
    JNIEnv* env = get_env();
    if (!env || !g_sse_callback_obj) return;

    if (is_done) {
        env->CallVoidMethod(g_sse_callback_obj, g_on_sse_done);
    } else {
        jstring jdata = env->NewStringUTF(json_data);
        env->CallVoidMethod(g_sse_callback_obj, g_on_sse_data, jdata);
        env->DeleteLocalRef(jdata);
    }
}

static void jni_agentic_callback(const char* event_json) {
    JNIEnv* env = get_env();
    if (!env || !g_agentic_callback_obj) return;

    jstring jdata = env->NewStringUTF(event_json);
    env->CallVoidMethod(g_agentic_callback_obj, g_on_agentic_event, jdata);
    env->DeleteLocalRef(jdata);
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeVersion(
    JNIEnv* env, jclass
) {
    return env->NewStringUTF(zetla_version());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeInit(
    JNIEnv* env, jclass
) {
    init_jvm(env);
    int ok = zetla_init();
    g_initialized = (ok != 0);
    nlohmann::json j;
    j["success"] = (ok != 0);
    if (!ok) {
        j["error"] = {{"code", "INIT_FAILED"}, {"message", "Failed to initialize"}};
    }
    return env->NewStringUTF(j.dump().c_str());
}

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeShutdown(
    JNIEnv* env, jclass
) {
    zetla_shutdown();
    g_initialized = false;
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeInitWithPath(
    JNIEnv* env, jclass, jstring storage_path
) {
    init_jvm(env);
    std::string path = jstring_to_string(env, storage_path);
    int ok = zetla_init_ex(path.c_str());
    g_initialized = (ok != 0);
    nlohmann::json j;
    j["success"] = (ok != 0);
    if (!ok) {
        j["error"] = {{"code", "INIT_FAILED"}, {"message", "Failed to initialize"}};
    }
    return env->NewStringUTF(j.dump().c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeCreateSession(
    JNIEnv* env, jclass, jstring model, jstring system_prompt
) {
    std::string m = jstring_to_string(env, model);
    std::string sp = jstring_to_string(env, system_prompt);
    zetla_response resp = zetla_create_session(m.c_str(), sp.c_str());
    std::string result = resp.success
        ? std::string(resp.data)
        : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeCreateSpace(
    JNIEnv* env, jclass, jstring model, jstring system_prompt
) {
    std::string m = jstring_to_string(env, model);
    std::string sp = jstring_to_string(env, system_prompt);
    zetla_response resp = zetla_create_space(m.c_str(), sp.c_str());
    std::string result = resp.success
        ? std::string(resp.data)
        : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jboolean JNICALL Java_com_zetla_data_ZetlaCore_nativeSendMessage(
    JNIEnv* env, jclass, jstring session_id, jstring message, jobject callback
) {
    init_jvm(env);

    std::lock_guard<std::mutex> lock(g_jni_mutex);
    if (g_callback_obj) {
        env->DeleteGlobalRef(g_callback_obj);
        g_callback_obj = nullptr;
    }
    if (g_callback_class_global) {
        env->DeleteGlobalRef(g_callback_class_global);
        g_callback_class_global = nullptr;
    }

    jclass local_class = env->GetObjectClass(callback);
    g_callback_class_global = (jclass)env->NewGlobalRef(local_class);
    env->DeleteLocalRef(local_class);

    g_on_token = env->GetMethodID(g_callback_class_global, "onToken", "(Ljava/lang/String;)V");
    g_on_finished = env->GetMethodID(g_callback_class_global, "onFinished", "()V");
    g_callback_obj = env->NewGlobalRef(callback);

    std::string sid = jstring_to_string(env, session_id);
    std::string msg = jstring_to_string(env, message);

    int ok = zetla_send_message(sid.c_str(), msg.c_str(), jni_stream_callback);

    if (g_callback_obj) {
        env->DeleteGlobalRef(g_callback_obj);
        g_callback_obj = nullptr;
    }

    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_zetla_data_ZetlaCore_nativeSendMessageSse(
    JNIEnv* env, jclass, jstring session_id, jstring message, jobject callback
) {
    init_jvm(env);

    std::lock_guard<std::mutex> lock(g_jni_mutex);
    if (g_sse_callback_obj) {
        env->DeleteGlobalRef(g_sse_callback_obj);
        g_sse_callback_obj = nullptr;
    }
    if (g_sse_callback_class_global) {
        env->DeleteGlobalRef(g_sse_callback_class_global);
        g_sse_callback_class_global = nullptr;
    }

    jclass local_class = env->GetObjectClass(callback);
    g_sse_callback_class_global = (jclass)env->NewGlobalRef(local_class);
    env->DeleteLocalRef(local_class);

    g_on_sse_data = env->GetMethodID(g_sse_callback_class_global, "onSseData", "(Ljava/lang/String;)V");
    g_on_sse_done = env->GetMethodID(g_sse_callback_class_global, "onSseDone", "()V");
    g_sse_callback_obj = env->NewGlobalRef(callback);

    std::string sid = jstring_to_string(env, session_id);
    std::string msg = jstring_to_string(env, message);

    int ok = zetla_send_message_sse(sid.c_str(), msg.c_str(), jni_sse_callback);

    if (g_sse_callback_obj) {
        env->DeleteGlobalRef(g_sse_callback_obj);
        g_sse_callback_obj = nullptr;
    }

    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_zetla_data_ZetlaCore_nativeSendMessageAgentic(
    JNIEnv* env, jclass, jstring session_id, jstring message, jobject callback
) {
    init_jvm(env);

    std::lock_guard<std::mutex> lock(g_jni_mutex);
    if (g_agentic_callback_obj) {
        env->DeleteGlobalRef(g_agentic_callback_obj);
        g_agentic_callback_obj = nullptr;
    }
    if (g_agentic_callback_class_global) {
        env->DeleteGlobalRef(g_agentic_callback_class_global);
        g_agentic_callback_class_global = nullptr;
    }

    jclass local_class = env->GetObjectClass(callback);
    g_agentic_callback_class_global = (jclass)env->NewGlobalRef(local_class);
    env->DeleteLocalRef(local_class);

    g_on_agentic_event = env->GetMethodID(g_agentic_callback_class_global, "onEvent", "(Ljava/lang/String;)V");
    g_agentic_callback_obj = env->NewGlobalRef(callback);

    std::string sid = jstring_to_string(env, session_id);
    std::string msg = jstring_to_string(env, message);

    int ok = zetla_send_message_agentic(sid.c_str(), msg.c_str(), jni_agentic_callback);

    if (g_agentic_callback_obj) {
        env->DeleteGlobalRef(g_agentic_callback_obj);
        g_agentic_callback_obj = nullptr;
    }

    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeDeleteSession(
    JNIEnv* env, jclass, jstring session_id
) {
    std::string sid = jstring_to_string(env, session_id);
    zetla_response resp = zetla_delete_session(sid.c_str());
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeGetSessionInfo(
    JNIEnv* env, jclass, jstring session_id
) {
    std::string sid = jstring_to_string(env, session_id);
    zetla_response resp = zetla_get_session_info(sid.c_str());
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeGetHistory(
    JNIEnv* env, jclass, jstring session_id
) {
    std::string sid = jstring_to_string(env, session_id);
    zetla_response resp = zetla_get_history(sid.c_str());
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeClearHistory(
    JNIEnv* env, jclass, jstring session_id
) {
    std::string sid = jstring_to_string(env, session_id);
    zetla_response resp = zetla_clear_history(sid.c_str());
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeSetSessionOptions(
    JNIEnv* env, jclass, jstring session_id, jstring options_json
) {
    std::string sid = jstring_to_string(env, session_id);
    std::string opts = jstring_to_string(env, options_json);
    zetla_response resp = zetla_set_session_options(sid.c_str(), opts.c_str());
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeGetSessionOptions(
    JNIEnv* env, jclass, jstring session_id
) {
    std::string sid = jstring_to_string(env, session_id);
    zetla_response resp = zetla_get_session_options(sid.c_str());
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeSetSessionModel(
    JNIEnv* env, jclass /*cls*/, jstring session_id, jstring model
) {
    std::string sid = jstring_to_string(env, session_id);
    std::string m = jstring_to_string(env, model);
    zetla_response resp = zetla_set_session_model(sid.c_str(), m.c_str());
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeLoadSession(
    JNIEnv* env, jclass, jstring session_id
) {
    std::string sid = jstring_to_string(env, session_id);
    zetla_response resp = zetla_load_session(sid.c_str());
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeListSessions(
    JNIEnv* env, jclass
) {
    zetla_response resp = zetla_list_sessions();
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeDeleteFromStorage(
    JNIEnv* env, jclass, jstring session_id
) {
    std::string sid = jstring_to_string(env, session_id);
    zetla_response resp = zetla_delete_from_storage(sid.c_str());
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeSessionExistsOnDisk(
    JNIEnv* env, jclass, jstring session_id
) {
    std::string sid = jstring_to_string(env, session_id);
    zetla_response resp = zetla_session_exists_on_disk(sid.c_str());
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeCompactSession(
    JNIEnv* env, jclass, jstring session_id
) {
    std::string sid = jstring_to_string(env, session_id);
    zetla_response resp = zetla_compact_session(sid.c_str());
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeGetCompactionInfo(
    JNIEnv* env, jclass, jstring session_id
) {
    std::string sid = jstring_to_string(env, session_id);
    zetla_response resp = zetla_get_compaction_info(sid.c_str());
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeAddTool(
    JNIEnv* env, jclass, jstring session_id, jstring name, jstring description, jstring parameters_schema
) {
    std::string sid = jstring_to_string(env, session_id);
    std::string n = jstring_to_string(env, name);
    std::string d = jstring_to_string(env, description);
    std::string ps = jstring_to_string(env, parameters_schema);
    zetla_response resp = zetla_add_tool(sid.c_str(), n.c_str(), d.c_str(), ps.c_str());
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeSetApiKey(
    JNIEnv* env, jclass, jstring api_key
) {
    std::string key = jstring_to_string(env, api_key);
    zetla_set_api_key(key.c_str());
}

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeSetModel(
    JNIEnv* env, jclass /*cls*/, jstring model
) {
    std::string m = jstring_to_string(env, model);
    zetla_set_model(m.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeListProviders(
    JNIEnv* env, jclass /*cls*/
) {
    zetla_response resp = zetla_list_providers();
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeSetProvider(
    JNIEnv* env, jclass /*cls*/, jstring provider_id
) {
    std::string pid = jstring_to_string(env, provider_id);
    zetla_response resp = zetla_set_provider(pid.c_str());
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeListModels(
    JNIEnv* env, jclass /*cls*/
) {
    zetla_response resp = zetla_list_models();
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeAddFile(
    JNIEnv* env, jclass, jstring session_id, jstring file_path
) {
    std::string sid = jstring_to_string(env, session_id);
    std::string fp = jstring_to_string(env, file_path);
    zetla_response resp = zetla_add_file(sid.c_str(), fp.c_str());
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeRemoveFile(
    JNIEnv* env, jclass, jstring session_id, jstring file_id
) {
    std::string sid = jstring_to_string(env, session_id);
    std::string fid = jstring_to_string(env, file_id);
    zetla_response resp = zetla_remove_file(sid.c_str(), fid.c_str());
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeListFiles(
    JNIEnv* env, jclass, jstring session_id
) {
    std::string sid = jstring_to_string(env, session_id);
    zetla_response resp = zetla_list_files(sid.c_str());
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jboolean JNICALL Java_com_zetla_data_ZetlaCore_nativeSendMessageWithFiles(
    JNIEnv* env, jclass, jstring session_id, jstring message,
    jobjectArray file_ids, jobject callback
) {
    init_jvm(env);

    std::lock_guard<std::mutex> lock(g_jni_mutex);
    if (g_callback_obj) {
        env->DeleteGlobalRef(g_callback_obj);
        g_callback_obj = nullptr;
    }
    if (g_callback_class_global) {
        env->DeleteGlobalRef(g_callback_class_global);
        g_callback_class_global = nullptr;
    }

    jclass local_class = env->GetObjectClass(callback);
    g_callback_class_global = (jclass)env->NewGlobalRef(local_class);
    env->DeleteLocalRef(local_class);

    g_on_token = env->GetMethodID(g_callback_class_global, "onToken", "(Ljava/lang/String;)V");
    g_on_finished = env->GetMethodID(g_callback_class_global, "onFinished", "()V");
    g_callback_obj = env->NewGlobalRef(callback);

    std::string sid = jstring_to_string(env, session_id);
    std::string msg = jstring_to_string(env, message);

    int count = env->GetArrayLength(file_ids);
    std::vector<std::string> fid_strs;
    fid_strs.reserve(count);
    for (int i = 0; i < count; i++) {
        auto jstr = (jstring)env->GetObjectArrayElement(file_ids, i);
        fid_strs.push_back(jstring_to_string(env, jstr));
        env->DeleteLocalRef(jstr);
    }

    std::vector<const char*> fid_cstrs;
    fid_cstrs.reserve(count);
    for (auto& s : fid_strs) fid_cstrs.push_back(s.c_str());

    int ok = zetla_send_message_with_files(sid.c_str(), msg.c_str(), fid_cstrs.data(), count, jni_stream_callback);

    if (g_callback_obj) {
        env->DeleteGlobalRef(g_callback_obj);
        g_callback_obj = nullptr;
    }

    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeSetDefaultOptions(
    JNIEnv* env, jclass, jstring options_json
) {
    std::string json = jstring_to_string(env, options_json);
    zetla_set_default_options(json.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeGetDefaultOptions(
    JNIEnv* env, jclass
) {
    zetla_response resp = zetla_get_default_options();
    std::string result = resp.success ? std::string(resp.data) : "{}";
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeSetSystemPrompt(
    JNIEnv* env, jclass, jstring system_prompt
) {
    std::string sp = jstring_to_string(env, system_prompt);
    zetla_set_system_prompt(sp.c_str());
}

JNIEXPORT jboolean JNICALL Java_com_zetla_data_ZetlaCore_nativeSetSessionSystemPrompt(
    JNIEnv* env, jclass, jstring session_id, jstring system_prompt
) {
    std::string sid = jstring_to_string(env, session_id);
    std::string sp = jstring_to_string(env, system_prompt);
    zetla_response resp = zetla_set_session_system_prompt(sid.c_str(), sp.c_str());
    bool ok = resp.success;
    zetla_free_response(&resp);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeGetSystemPrompt(
    JNIEnv* env, jclass
) {
    zetla_response resp = zetla_get_system_prompt();
    std::string result = resp.success
        ? std::string(resp.data)
        : nlohmann::json({{"system_prompt", ""}}).dump();
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeSetProviderConfig(
    JNIEnv* env, jclass, jstring provider_id, jstring api_key, jstring base_url, jboolean enabled
) {
    std::string pid = jstring_to_string(env, provider_id);
    std::string ak = jstring_to_string(env, api_key);
    std::string bu = jstring_to_string(env, base_url);
    zetla_set_provider_config(pid.c_str(), ak.c_str(), bu.c_str(), enabled ? 1 : 0);
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeGetProviderConfig(
    JNIEnv* env, jclass, jstring provider_id
) {
    std::string pid = jstring_to_string(env, provider_id);
    zetla_response resp = zetla_get_provider_config(pid.c_str());
    std::string result = resp.success ? std::string(resp.data) : "{}";
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeListProviderConfigs(
    JNIEnv* env, jclass
) {
    zetla_response resp = zetla_list_provider_configs();
    std::string result = resp.success ? std::string(resp.data) : "[]";
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeListProvidersModels(
    JNIEnv* env, jclass
) {
    zetla_response resp = zetla_list_providers_models();
    std::string result = resp.success ? std::string(resp.data) : "[]";
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jboolean JNICALL Java_com_zetla_data_ZetlaCore_nativeSetSessionWebSearch(
    JNIEnv* env, jclass, jstring session_id, jboolean enabled
) {
    std::string sid = jstring_to_string(env, session_id);
    int result = zetla_set_session_web_search(sid.c_str(), enabled ? 1 : 0);
    return result ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeSetSearchProvider(
    JNIEnv* env, jclass, jstring provider
) {
    std::string p = jstring_to_string(env, provider);
    zetla_set_search_provider(p.c_str());
}

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeSetExaApiKey(
    JNIEnv* env, jclass, jstring api_key
) {
    std::string key = jstring_to_string(env, api_key);
    zetla_set_exa_api_key(key.c_str());
}

// C callback that bridges to Java ToolExecutorCallback
static char* jni_tool_executor_callback(const char* session_id, const char* tool_name, const char* arguments_json) {
    JNIEnv* env = get_env();
    if (!env || !g_tool_executor_obj) {
        char* err = new char[3];
        err[0] = '{'; err[1] = '}'; err[2] = 0;
        return err;
    }

    jstring jsid = env->NewStringUTF(session_id);
    jstring jname = env->NewStringUTF(tool_name);
    jstring jargs = env->NewStringUTF(arguments_json);

    jstring result = (jstring)env->CallObjectMethod(g_tool_executor_obj, g_tool_executor_execute, jsid, jname, jargs);

    env->DeleteLocalRef(jsid);
    env->DeleteLocalRef(jname);
    env->DeleteLocalRef(jargs);

    const char* result_str = result ? env->GetStringUTFChars(result, nullptr) : "{}";
    size_t len = strlen(result_str);
    char* out = new char[len + 1];
    memcpy(out, result_str, len + 1);

    if (result) {
        env->ReleaseStringUTFChars(result, result_str);
        env->DeleteLocalRef(result);
    }

    return out;
}

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeSetToolExecutor(
    JNIEnv* env, jclass, jstring session_id, jobject callback
) {
    std::lock_guard<std::mutex> lock(g_jni_mutex);

    // Clean up previous
    if (g_tool_executor_obj) {
        env->DeleteGlobalRef(g_tool_executor_obj);
        g_tool_executor_obj = nullptr;
    }
    if (g_tool_executor_class_global) {
        env->DeleteGlobalRef(g_tool_executor_class_global);
        g_tool_executor_class_global = nullptr;
    }

    if (callback == nullptr) {
        zetla_set_tool_executor(nullptr);
        return;
    }

    jclass local_class = env->GetObjectClass(callback);
    g_tool_executor_class_global = (jclass)env->NewGlobalRef(local_class);
    env->DeleteLocalRef(local_class);

    g_tool_executor_execute = env->GetMethodID(g_tool_executor_class_global, "execute",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");

    g_tool_executor_obj = env->NewGlobalRef(callback);

    zetla_set_tool_executor(jni_tool_executor_callback);
}

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeCancelRequest(
    JNIEnv* env, jclass
) {
    zetla_cancel_request();
}

JNIEXPORT jboolean JNICALL Java_com_zetla_data_ZetlaCore_nativeSendMessageWithImages(
    JNIEnv* env, jclass, jstring session_id, jstring message,
    jobjectArray image_data_uris, jobject callback
) {
    init_jvm(env);

    std::lock_guard<std::mutex> lock(g_jni_mutex);
    if (g_callback_obj) {
        env->DeleteGlobalRef(g_callback_obj);
        g_callback_obj = nullptr;
    }
    if (g_callback_class_global) {
        env->DeleteGlobalRef(g_callback_class_global);
        g_callback_class_global = nullptr;
    }

    jclass local_class = env->GetObjectClass(callback);
    g_callback_class_global = (jclass)env->NewGlobalRef(local_class);
    env->DeleteLocalRef(local_class);

    g_on_token = env->GetMethodID(g_callback_class_global, "onToken", "(Ljava/lang/String;)V");
    g_on_finished = env->GetMethodID(g_callback_class_global, "onFinished", "()V");
    g_callback_obj = env->NewGlobalRef(callback);

    std::string sid = jstring_to_string(env, session_id);
    std::string msg = jstring_to_string(env, message);

    int count = env->GetArrayLength(image_data_uris);
    std::vector<std::string> uri_strs;
    uri_strs.reserve(count);
    for (int i = 0; i < count; i++) {
        auto jstr = (jstring)env->GetObjectArrayElement(image_data_uris, i);
        uri_strs.push_back(jstring_to_string(env, jstr));
        env->DeleteLocalRef(jstr);
    }

    std::vector<const char*> uri_cstrs;
    uri_cstrs.reserve(count);
    for (auto& s : uri_strs) uri_cstrs.push_back(s.c_str());

    int ok = zetla_send_message_with_images(sid.c_str(), msg.c_str(), uri_cstrs.data(), count, jni_stream_callback);

    if (g_callback_obj) {
        env->DeleteGlobalRef(g_callback_obj);
        g_callback_obj = nullptr;
    }

    return ok ? JNI_TRUE : JNI_FALSE;
}

// 
// RAG (Hyperbolic Search) JNI Bridge
// 

#include "../rag/rag_tool.hpp"

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeRagInit(
    JNIEnv* env, jclass, jstring model_path) {
    const char* mp = env->GetStringUTFChars(model_path, nullptr);
    bool ok = zetla::rag::RagManager::instance().init_embedder(mp);
    env->ReleaseStringUTFChars(model_path, mp);
    return env->NewStringUTF(ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeRagAddFile(
    JNIEnv* env, jclass, jstring session_id, jstring file_path, jstring text_content) {
    const char* sid = env->GetStringUTFChars(session_id, nullptr);
    const char* fp  = env->GetStringUTFChars(file_path, nullptr);
    const char* txt = env->GetStringUTFChars(text_content, nullptr);

    zetla::rag::RagManager::instance().create_session(sid);
    int n = zetla::rag::RagManager::instance().add_file(sid, fp, txt);

    env->ReleaseStringUTFChars(session_id, sid);
    env->ReleaseStringUTFChars(file_path, fp);
    env->ReleaseStringUTFChars(text_content, txt);

    char buf[128];
    snprintf(buf, sizeof(buf), "{\"chunks\":%d}", n);
    return env->NewStringUTF(buf);
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeRagSearch(
    JNIEnv* env, jclass, jstring session_id, jstring query, jint top_k, jstring scope_file) {
    const char* sid = env->GetStringUTFChars(session_id, nullptr);
    const char* q   = env->GetStringUTFChars(query, nullptr);
    const char* sf  = scope_file ? env->GetStringUTFChars(scope_file, nullptr) : nullptr;

    std::string result = zetla::rag::RagManager::instance().search(
        sid, q, top_k, sf ? sf : "");

    env->ReleaseStringUTFChars(session_id, sid);
    env->ReleaseStringUTFChars(query, q);
    if (sf) env->ReleaseStringUTFChars(scope_file, sf);

    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jint JNICALL Java_com_zetla_data_ZetlaCore_nativeRagChunkCount(
    JNIEnv* env, jclass, jstring session_id) {
    const char* sid = env->GetStringUTFChars(session_id, nullptr);
    int n = zetla::rag::RagManager::instance().chunk_count(sid);
    env->ReleaseStringUTFChars(session_id, sid);
    return n;
}

JNIEXPORT jlong JNICALL Java_com_zetla_data_ZetlaCore_nativeRagMemoryBytes(
    JNIEnv* env, jclass, jstring session_id) {
    const char* sid = env->GetStringUTFChars(session_id, nullptr);
    size_t n = zetla::rag::RagManager::instance().memory_bytes(sid);
    env->ReleaseStringUTFChars(session_id, sid);
    return (jlong)n;
}

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeRagRemoveSession(
    JNIEnv* env, jclass, jstring session_id) {
    const char* sid = env->GetStringUTFChars(session_id, nullptr);
    zetla::rag::RagManager::instance().remove_session(sid);
    env->ReleaseStringUTFChars(session_id, sid);
}

static jobject g_rag_debug_obj = nullptr;

static void rag_debug_callback(const char* msg) {
    if (!g_rag_debug_obj) return;
    JNIEnv* env = get_env();
    if (!env) return;
    jclass cls = env->GetObjectClass(g_rag_debug_obj);
    jmethodID mid = env->GetMethodID(cls, "onDebug", "(Ljava/lang/String;)V");
    if (mid) {
        jstring jmsg = env->NewStringUTF(msg);
        env->CallVoidMethod(g_rag_debug_obj, mid, jmsg);
        env->DeleteLocalRef(jmsg);
    }
    env->DeleteLocalRef(cls);
}

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeRagSetDebugCallback(
    JNIEnv* env, jclass, jobject callback) {
    if (g_rag_debug_obj) env->DeleteGlobalRef(g_rag_debug_obj);
    g_rag_debug_obj = callback ? env->NewGlobalRef(callback) : nullptr;
    zetla::rag::RagManager::instance().set_debug_callback(
        callback ? rag_debug_callback : nullptr);
}

JNIEXPORT jint JNICALL Java_com_zetla_data_ZetlaCore_nativeSetSessionRag(
    JNIEnv* env, jclass, jstring session_id, jint enabled) {
    const char* sid = env->GetStringUTFChars(session_id, nullptr);
    int result = zetla_set_session_rag(sid, enabled);
    env->ReleaseStringUTFChars(session_id, sid);
    return result;
}

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeInitRagModel(
    JNIEnv* env, jclass, jstring model_dir) {
    const char* md = env->GetStringUTFChars(model_dir, nullptr);
    zetla_init_rag_model(md);
    env->ReleaseStringUTFChars(model_dir, md);
}

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeSetProjectionEnabled(
    JNIEnv* env, jclass, jboolean enabled) {
    zetla::rag::RagManager::instance().set_projection_enabled(enabled);
}

JNIEXPORT void JNICALL Java_com_zetla_data_ZetlaCore_nativeSetRagConfig(
    JNIEnv* env, jclass, jstring config_json) {
    const char* json = env->GetStringUTFChars(config_json, nullptr);
    zetla_set_rag_config_json(json);
    env->ReleaseStringUTFChars(config_json, json);
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeGetRagConfig(
    JNIEnv* env, jclass) {
    char* result = zetla_get_rag_config_json();
    jstring js = env->NewStringUTF(result);
    zetla_free_string(result);
    return js;
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeAddSpaceFile(
    JNIEnv* env, jclass, jstring session_id, jstring file_path, jstring text_content) {
    const char* sid = env->GetStringUTFChars(session_id, nullptr);
    const char* fp  = env->GetStringUTFChars(file_path, nullptr);
    const char* txt = env->GetStringUTFChars(text_content, nullptr);

    zetla_response resp = zetla_add_space_file(sid, fp, txt);
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);

    env->ReleaseStringUTFChars(session_id, sid);
    env->ReleaseStringUTFChars(file_path, fp);
    env->ReleaseStringUTFChars(text_content, txt);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeListSpaceFiles(
    JNIEnv* env, jclass, jstring session_id) {
    const char* sid = env->GetStringUTFChars(session_id, nullptr);
    zetla_response resp = zetla_list_space_files(sid);
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    env->ReleaseStringUTFChars(session_id, sid);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeIsSpace(
    JNIEnv* env, jclass, jstring session_id) {
    const char* sid = env->GetStringUTFChars(session_id, nullptr);
    zetla_response resp = zetla_is_space(sid);
    std::string result = resp.success ? std::string(resp.data) : std::string(resp.error);
    env->ReleaseStringUTFChars(session_id, sid);
    zetla_free_response(&resp);
    return env->NewStringUTF(result.c_str());
}

JNIEXPORT jboolean JNICALL Java_com_zetla_data_ZetlaCore_nativeSaveRagSession(
    JNIEnv* env, jclass, jstring session_id, jstring dir_path) {
    const char* sid = env->GetStringUTFChars(session_id, nullptr);
    const char* dir = env->GetStringUTFChars(dir_path, nullptr);
    int result = zetla_rag_save_session(sid, dir);
    env->ReleaseStringUTFChars(session_id, sid);
    env->ReleaseStringUTFChars(dir_path, dir);
    return result == ZETLA_RAG_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_zetla_data_ZetlaCore_nativeLoadRagSession(
    JNIEnv* env, jclass, jstring session_id, jstring dir_path) {
    const char* sid = env->GetStringUTFChars(session_id, nullptr);
    const char* dir = env->GetStringUTFChars(dir_path, nullptr);
    int result = zetla_rag_load_session(sid, dir);
    env->ReleaseStringUTFChars(session_id, sid);
    env->ReleaseStringUTFChars(dir_path, dir);
    return result == ZETLA_RAG_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL Java_com_zetla_data_ZetlaCore_nativeExtractFileText(
    JNIEnv* env, jclass, jstring file_path) {
    const char* fp = env->GetStringUTFChars(file_path, nullptr);
    char* result = zetla_extract_file_text(fp);
    jstring js = env->NewStringUTF(result);
    zetla_free_string(result);
    env->ReleaseStringUTFChars(file_path, fp);
    return js;
}
