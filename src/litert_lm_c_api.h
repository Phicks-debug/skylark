// LiteRT-LM C API declarations for linking against liblitert-lm.dylib
// Extracted from the LiteRT-LM repository: /tmp/LiteRT-LM/c/engine.h
// This header mirrors the public C API surface.

#ifndef LITERT_LM_C_API_H
#define LITERT_LM_C_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- Opaque pointer types ----
typedef struct LiteRtLmEngineSettings LiteRtLmEngineSettings;
typedef struct LiteRtLmEngine LiteRtLmEngine;
typedef struct LiteRtLmSessionConfig LiteRtLmSessionConfig;
typedef struct LiteRtLmSession LiteRtLmSession;
typedef struct LiteRtLmResponses LiteRtLmResponses;
typedef struct LiteRtLmConversationConfig LiteRtLmConversationConfig;
typedef struct LiteRtLmConversation LiteRtLmConversation;
typedef struct LiteRtLmConversationOptionalArgs LiteRtLmConversationOptionalArgs;
typedef struct LiteRtLmJsonResponse LiteRtLmJsonResponse;
typedef struct LiteRtLmBenchmarkInfo LiteRtLmBenchmarkInfo;
typedef struct LiteRtLmTokenUnion LiteRtLmTokenUnion;
typedef struct LiteRtLmTokenUnions LiteRtLmTokenUnions;
typedef struct LiteRtLmDetokenizeResult LiteRtLmDetokenizeResult;
typedef struct LiteRtLmTokenizeResult LiteRtLmTokenizeResult;

// ---- Enums ----
typedef enum {
    kLiteRtLmSamplerTypeUnspecified = 0,
    kLiteRtLmSamplerTypeTopK = 1,
    kLiteRtLmSamplerTypeTopP = 2,
    kLiteRtLmSamplerTypeGreedy = 3,
} LiteRtLmSamplerType;

typedef enum {
    kLiteRtLmInputDataTypeText = 0,
    kLiteRtLmInputDataTypeImage = 1,
    kLiteRtLmInputDataTypeImageEnd = 2,
    kLiteRtLmInputDataTypeAudio = 3,
    kLiteRtLmInputDataTypeAudioEnd = 4,
} LiteRtLmInputDataType;

typedef enum {
    kLiteRtLmTokenUnionTypeString = 0,
    kLiteRtLmTokenUnionTypeIds = 1,
} LiteRtLmTokenUnionType;

// ---- Structs ----
typedef struct {
    LiteRtLmSamplerType type;
    int32_t top_k;
    float top_p;
    float temperature;
    int32_t seed;
} LiteRtLmSamplerParams;

typedef struct {
    LiteRtLmInputDataType type;
    const void* data;
    size_t size;
} LiteRtLmInputData;

// ---- Stream callback ----
typedef void (*LiteRtLmStreamCallback)(void* callback_data,
                                       const char* chunk,
                                       bool is_final,
                                       const char* error_msg);

// ---- Logging ----
void litert_lm_set_min_log_level(int level);

// ---- Engine Settings ----
LiteRtLmEngineSettings* litert_lm_engine_settings_create(
    const char* model_path,
    const char* backend_str,
    const char* vision_backend_str,
    const char* audio_backend_str);
void litert_lm_engine_settings_delete(LiteRtLmEngineSettings* settings);
void litert_lm_engine_settings_set_max_num_tokens(
    LiteRtLmEngineSettings* settings, int max_num_tokens);
void litert_lm_engine_settings_set_cache_dir(
    LiteRtLmEngineSettings* settings, const char* cache_dir);
void litert_lm_engine_settings_set_litert_dispatch_lib_dir(
    LiteRtLmEngineSettings* settings, const char* dispatch_dir);
void litert_lm_engine_settings_set_enable_speculative_decoding(
    LiteRtLmEngineSettings* settings, bool enable_speculative_decoding);
void litert_lm_engine_settings_enable_benchmark(
    LiteRtLmEngineSettings* settings, bool enable);
void litert_lm_engine_settings_set_num_prefill_tokens(
    LiteRtLmEngineSettings* settings, int num_prefill_tokens);
void litert_lm_engine_settings_set_num_decode_tokens(
    LiteRtLmEngineSettings* settings, int num_decode_tokens);

// ---- Engine ----
LiteRtLmEngine* litert_lm_engine_create(
    const LiteRtLmEngineSettings* settings);
void litert_lm_engine_delete(LiteRtLmEngine* engine);

// ---- Session Config ----
LiteRtLmSessionConfig* litert_lm_session_config_create(void);
void litert_lm_session_config_delete(LiteRtLmSessionConfig* config);
void litert_lm_session_config_set_max_output_tokens(
    LiteRtLmSessionConfig* config, int max_output_tokens);
void litert_lm_session_config_set_apply_prompt_template(
    LiteRtLmSessionConfig* config, bool apply_prompt_template);
void litert_lm_session_config_set_sampler_params(
    LiteRtLmSessionConfig* config,
    const LiteRtLmSamplerParams* sampler_params);

// ---- Conversation Config ----
LiteRtLmConversationConfig* litert_lm_conversation_config_create(void);
void litert_lm_conversation_config_delete(LiteRtLmConversationConfig* config);
void litert_lm_conversation_config_set_session_config(
    LiteRtLmConversationConfig* config,
    const LiteRtLmSessionConfig* session_config);
void litert_lm_conversation_config_set_system_message(
    LiteRtLmConversationConfig* config, const char* system_message_json);
void litert_lm_conversation_config_set_tools(
    LiteRtLmConversationConfig* config, const char* tools_json);
void litert_lm_conversation_config_set_messages(
    LiteRtLmConversationConfig* config, const char* messages_json);
void litert_lm_conversation_config_set_extra_context(
    LiteRtLmConversationConfig* config, const char* extra_context_json);
void litert_lm_conversation_config_set_enable_constrained_decoding(
    LiteRtLmConversationConfig* config, bool enable_constrained_decoding);
void litert_lm_conversation_config_set_filter_channel_content_from_kv_cache(
    LiteRtLmConversationConfig* config,
    bool filter_channel_content_from_kv_cache);

// ---- Conversation Optional Args ----
LiteRtLmConversationOptionalArgs* litert_lm_conversation_optional_args_create(void);
void litert_lm_conversation_optional_args_delete(
    LiteRtLmConversationOptionalArgs* optional_args);
void litert_lm_conversation_optional_args_set_visual_token_budget(
    LiteRtLmConversationOptionalArgs* optional_args, int visual_token_budget);

// ---- Conversation ----
LiteRtLmConversation* litert_lm_conversation_create(
    LiteRtLmEngine* engine, LiteRtLmConversationConfig* config);
void litert_lm_conversation_delete(LiteRtLmConversation* conversation);
int litert_lm_conversation_send_message_stream(
    LiteRtLmConversation* conversation,
    const char* message_json,
    const char* extra_context,
    const LiteRtLmConversationOptionalArgs* optional_args,
    LiteRtLmStreamCallback callback,
    void* callback_data);
LiteRtLmJsonResponse* litert_lm_conversation_send_message(
    LiteRtLmConversation* conversation,
    const char* message_json,
    const char* extra_context,
    const LiteRtLmConversationOptionalArgs* optional_args);
void litert_lm_conversation_cancel_process(LiteRtLmConversation* conversation);
const char* litert_lm_conversation_render_message_to_string(
    LiteRtLmConversation* conversation, const char* message_json);

// ---- JSON Response ----
const char* litert_lm_json_response_get_string(
    const LiteRtLmJsonResponse* response);
void litert_lm_json_response_delete(LiteRtLmJsonResponse* response);

// ---- Responses ----
void litert_lm_responses_delete(LiteRtLmResponses* responses);
int litert_lm_responses_get_num_candidates(
    const LiteRtLmResponses* responses);
const char* litert_lm_responses_get_response_text_at(
    const LiteRtLmResponses* responses, int index);

#ifdef __cplusplus
}
#endif

#endif // LITERT_LM_C_API_H
