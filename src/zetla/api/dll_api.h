#pragma once

#if defined(_WIN32) || defined(_WIN64)
  #ifdef ZETLA_DLL_EXPORTS
    #define ZETLA_API __declspec(dllexport)
  #else
    #define ZETLA_API __declspec(dllimport)
  #endif
#else
  #define ZETLA_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*zetla_token_fn)(const char* json_chunk, int is_finished);

typedef void (*zetla_sse_fn)(const char* json_data, int is_done);

typedef void (*zetla_agentic_fn)(const char* event_json);

typedef char* (*zetla_tool_executor_fn)(const char* session_id, const char* tool_name, const char* arguments_json);

typedef struct {
    int success;
    char* data;
    char* error;
} zetla_response;

ZETLA_API const char* zetla_version(void);
ZETLA_API int zetla_init(void);
ZETLA_API int zetla_init_ex(const char* storage_path);
ZETLA_API void zetla_shutdown(void);
ZETLA_API void zetla_set_api_key(const char* api_key);
ZETLA_API void zetla_set_model(const char* model);
ZETLA_API zetla_response zetla_list_providers(void);
ZETLA_API zetla_response zetla_set_provider(const char* provider_id);
ZETLA_API zetla_response zetla_list_models(void);

ZETLA_API zetla_response zetla_create_session(const char* model, const char* system_prompt);
ZETLA_API zetla_response zetla_delete_session(const char* session_id);
ZETLA_API zetla_response zetla_get_session_info(const char* session_id);

ZETLA_API int zetla_send_message(const char* session_id, const char* message, zetla_token_fn callback);
ZETLA_API int zetla_send_message_sse(const char* session_id, const char* message, zetla_sse_fn callback);

ZETLA_API zetla_response zetla_set_session_options(const char* session_id, const char* options_json);
ZETLA_API zetla_response zetla_get_session_options(const char* session_id);
ZETLA_API zetla_response zetla_set_session_model(const char* session_id, const char* model);

ZETLA_API void zetla_set_default_options(const char* options_json);
ZETLA_API zetla_response zetla_get_default_options(void);

ZETLA_API void zetla_set_system_prompt(const char* system_prompt);
ZETLA_API zetla_response zetla_get_system_prompt(void);
ZETLA_API zetla_response zetla_set_session_system_prompt(const char* session_id, const char* system_prompt);

ZETLA_API void zetla_set_provider_config(const char* provider_id, const char* api_key, const char* base_url, int enabled);
ZETLA_API zetla_response zetla_get_provider_config(const char* provider_id);
ZETLA_API zetla_response zetla_list_provider_configs(void);
ZETLA_API zetla_response zetla_list_providers_models(void);

ZETLA_API int zetla_set_session_web_search(const char* session_id, int enabled);
ZETLA_API void zetla_set_search_provider(const char* provider);
ZETLA_API void zetla_set_exa_api_key(const char* api_key);

ZETLA_API zetla_response zetla_get_history(const char* session_id);
ZETLA_API zetla_response zetla_clear_history(const char* session_id);

ZETLA_API zetla_response zetla_load_session(const char* session_id);
ZETLA_API zetla_response zetla_list_sessions(void);
ZETLA_API zetla_response zetla_delete_from_storage(const char* session_id);
ZETLA_API zetla_response zetla_session_exists_on_disk(const char* session_id);

ZETLA_API zetla_response zetla_compact_session(const char* session_id);
ZETLA_API zetla_response zetla_get_compaction_info(const char* session_id);

ZETLA_API zetla_response zetla_add_tool(
    const char* session_id,
    const char* name,
    const char* description,
    const char* parameters_schema_json
);

ZETLA_API void zetla_set_tool_executor(zetla_tool_executor_fn fn);

ZETLA_API int zetla_send_message_agentic(
    const char* session_id,
    const char* message,
    zetla_agentic_fn callback
);

ZETLA_API int zetla_send_message_with_images(
    const char* session_id,
    const char* message,
    const char** image_data_uris,
    int image_count,
    zetla_token_fn callback
);

ZETLA_API int zetla_send_message_with_images_sse(
    const char* session_id,
    const char* message,
    const char** image_data_uris,
    int image_count,
    zetla_sse_fn callback
);

ZETLA_API zetla_response zetla_add_file(
    const char* session_id,
    const char* file_path
);

ZETLA_API zetla_response zetla_remove_file(
    const char* session_id,
    const char* file_id
);

ZETLA_API zetla_response zetla_list_files(const char* session_id);

ZETLA_API int zetla_send_message_with_files(
    const char* session_id,
    const char* message,
    const char** file_ids,
    int file_count,
    zetla_token_fn callback
);

ZETLA_API void zetla_cancel_request(void);
ZETLA_API void zetla_free_response(zetla_response* response);
ZETLA_API void zetla_free_string(char* str);

#ifdef __cplusplus
}
#endif
