#include <purple.h>
#include <json-glib/json-glib.h>
#include <curl/curl.h>
#include <gtk/gtk.h>

#include <account.h>
#include <accountopt.h>
#include <connection.h>
#include <conversation.h>
#include <debug.h>
#include <notify.h>
#include <request.h>

struct MistralResponse {
    char *memory;
    size_t size;
};

typedef struct _MistralAsyncData {
    PurpleConversation *conv;
    char *message;
    char *api_key;
    CURL *curl;
    struct curl_slist *headers;
    struct MistralResponse response;
} MistralAsyncData;

static void mistral_login(PurpleAccount *account);
static void mistral_close(PurpleConnection *gc);
static int mistral_send_im(PurpleConnection *gc, const char *who, const char *message, PurpleMessageFlags flags);
static char *send_to_mistral_api(const char *message, const char *api_key);
static char *create_mistral_json_payload(const char *message, const char *username, const char *status, const char *status_message, GList *history);
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp);
static GList *mistral_status_types(PurpleAccount *account);
static const char *mistral_list_icon(PurpleAccount *account, PurpleBuddy *buddy);
static gboolean mistral_async_send(gpointer user_data);
static void send_to_mistral_api_async(MistralAsyncData *data);


static void mistral_login(PurpleAccount *account) {
    PurpleConnection *gc = purple_account_get_connection(account);
    
    purple_debug_info("mistral", "Logging into Mistral AI\n");
    
    const char *api_key = purple_account_get_string(account, "api_key", "");
    if (!api_key || strlen(api_key) == 0) {
        purple_connection_error(gc, "Mistral API key not configured. Please set your API key in account settings.");
        return;
    }
    
    purple_connection_set_state(gc, PURPLE_CONNECTED);
    purple_debug_info("mistral", "Successfully connected to Mistral AI\n");
    
    PurpleBuddy *buddy = purple_find_buddy(account, "mistral-ai");
    if (!buddy) {
        buddy = purple_buddy_new(account, "mistral-ai", "Mistral AI");
        purple_blist_add_buddy(buddy, NULL, NULL, NULL);
    }
    purple_prpl_got_user_status(account, "mistral-ai", "available", NULL);
}

static void mistral_close(PurpleConnection *gc) {
    purple_debug_info("mistral", "Closing Mistral AI connection\n");
}

static int mistral_send_im(PurpleConnection *gc, const char *who, const char *message, PurpleMessageFlags flags) {
    PurpleAccount *account = purple_connection_get_account(gc);
    const char *api_key = purple_account_get_string(account, "api_key", "");
    
    purple_debug_info("mistral", "Sending message to Mistral AI: %s\n", message);
    
    PurpleConversation *conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, who, account);

    MistralAsyncData *data = g_new(MistralAsyncData, 1);
    data->conv = conv;
    data->message = g_strdup(message);
    data->api_key = g_strdup(api_key);

    g_idle_add((GSourceFunc)mistral_async_send, data);

    return 1;
}

static gboolean mistral_async_send(gpointer user_data) {
    MistralAsyncData *data = (MistralAsyncData *)user_data;

    data->response.memory = NULL;
    data->response.size = 0;

    data->curl = curl_easy_init();
    if (!data->curl) {
        purple_debug_error("mistral", "Failed to initialize CURL.\n");
        g_free(data->message);
        g_free(data->api_key);
        g_free(data);
        return FALSE;
    }

    PurpleAccount *account = purple_conversation_get_account(data->conv);
    const char *username = purple_account_get_username(account);
    PurplePresence *presence = purple_account_get_presence(account);
    PurpleStatus *active_status = purple_presence_get_active_status(presence);
    const char *status_id = purple_status_get_id(active_status);
    const char *status_message = purple_status_get_attr_string(active_status, "message");

    char *json_payload = create_mistral_json_payload(data->message, username, status_id, status_message, NULL);

    curl_easy_setopt(data->curl, CURLOPT_URL, "https://api.mistral.ai/v1/chat/completions");
    curl_easy_setopt(data->curl, CURLOPT_POSTFIELDS, json_payload);
    curl_easy_setopt(data->curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(data->curl, CURLOPT_WRITEDATA, &data->response);
    curl_easy_setopt(data->curl, CURLOPT_TIMEOUT, 30L);

    data->headers = NULL;
    data->headers = curl_slist_append(data->headers, "Content-Type: application/json");
    char *auth_header = g_strdup_printf("Authorization: Bearer %s", data->api_key);
    data->headers = curl_slist_append(data->headers, auth_header);
    curl_easy_setopt(data->curl, CURLOPT_HTTPHEADER, data->headers);

    send_to_mistral_api_async(data);

    g_free(json_payload);
    g_free(auth_header);

    return FALSE;
}

static void send_to_mistral_api_async(MistralAsyncData *data) {
    CURLcode res = curl_easy_perform(data->curl);

    long http_code = 0;
    if (res == CURLE_OK) {
        CURLcode info_res = curl_easy_getinfo(data->curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (info_res != CURLE_OK) {
            purple_debug_error("mistral", "Failed to get HTTP status code: %s\n", curl_easy_strerror(info_res));
        }
    }

    if (res != CURLE_OK) {
        purple_debug_error("mistral", "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        if (data->conv) {
            char *error_msg = g_strdup_printf("Error: Failed to connect to Mistral API: %s", curl_easy_strerror(res));
            purple_conversation_write(data->conv, "Mistral AI", error_msg,
                                    PURPLE_MESSAGE_RECV, time(NULL));
            g_free(error_msg);
        }
    } else {
        purple_debug_info("mistral", "HTTP Status: %ld\n", http_code);
        purple_debug_info("mistral", "Response: %s\n", data->response.memory);

        char *result = NULL;
        JsonParser *parser = json_parser_new();
        GError *error = NULL;

        if (!json_parser_load_from_data(parser, data->response.memory, -1, &error)) {
            purple_debug_error("mistral", "Failed to parse JSON response: %s\n", error->message);
            purple_debug_error("mistral", "Raw response: %s\n", data->response.memory);
            result = g_strdup_printf("Error: Invalid response from Mistral API. HTTP Status: %ld", http_code);
            g_error_free(error);
        } else {
            JsonNode *root = json_parser_get_root(parser);
            JsonObject *root_obj = json_node_get_object(root);

            if (json_object_has_member(root_obj, "error")) {
                JsonObject *error_obj = json_object_get_object_member(root_obj, "error");
                const char *error_msg = json_object_get_string_member(error_obj, "message");
                const char *error_type = json_object_get_string_member(error_obj, "type");
                const char *error_code = json_object_get_string_member(error_obj, "code");

                purple_debug_error("mistral", "API Error - Type: %s, Code: %s, Message: %s\n",
                                 error_type ? error_type : "unknown",
                                 error_code ? error_code : "unknown",
                                 error_msg ? error_msg : "unknown");

                result = g_strdup_printf("Mistral API Error (%s): %s (Code: %s)",
                                       error_type ? error_type : "unknown",
                                       error_msg ? error_msg : "Unknown error",
                                       error_code ? error_code : "unknown");
            } else if (json_object_has_member(root_obj, "choices")) {
                JsonArray *choices = json_object_get_array_member(root_obj, "choices");
                if (json_array_get_length(choices) > 0) {
                    JsonObject *choice = json_array_get_object_element(choices, 0);
                    JsonObject *message_obj = json_object_get_object_member(choice, "message");
                    const char *content = json_object_get_string_member(message_obj, "content");

                    if (content) {
                        char *cleaned_content = g_strdup(content);
                        char *p = cleaned_content;
                        while (*p) {
                            if (*p == '\0') {
                                *p = ' ';
                            }
                            p++;
                        }

                        result = cleaned_content;

                        if (data->conv && result) {
                            purple_conversation_write(data->conv, "Mistral AI", result,
                                                    PURPLE_MESSAGE_RECV, time(NULL));
                            g_free(result);
                        }

                    } else {
                        result = g_strdup("Error: No content in Mistral response.");
                        if (data->conv && result) {
                            purple_conversation_write(data->conv, "Mistral AI", result,
                                                    PURPLE_MESSAGE_RECV, time(NULL));
                            g_free(result);
                        }
                    }
                } else {
                    result = g_strdup("Error: No choices in Mistral response.");
                    if (data->conv && result) {
                        purple_conversation_write(data->conv, "Mistral AI", result,
                                                PURPLE_MESSAGE_RECV, time(NULL));
                        g_free(result);
                    }
                }
            } else {
                JsonGenerator *gen = json_generator_new();
                json_generator_set_root(gen, root);
                char *json_str = json_generator_to_data(gen, NULL);
                purple_debug_error("mistral", "Unexpected response format: %s\n", json_str);
                g_free(json_str);
                g_object_unref(gen);

                result = g_strdup_printf("Error: Unexpected response format from Mistral API. HTTP Status: %ld", http_code);
                if (data->conv && result) {
                    purple_conversation_write(data->conv, "Mistral AI", result,
                                            PURPLE_MESSAGE_RECV, time(NULL));
                    g_free(result);
                }
            }
        }
        g_object_unref(parser);
    }

    if (data->response.memory) {
        free(data->response.memory);
    }
    if (data->headers) {
        curl_slist_free_all(data->headers);
    }
    if (data->curl) {
        curl_easy_cleanup(data->curl);
    }
    g_free(data->message);
    g_free(data->api_key);
    g_free(data);
}

static char *create_mistral_json_payload(const char *message, const char *username, const char *status, const char *status_message, GList *history) {
    JsonBuilder *builder = json_builder_new();

    json_builder_begin_object(builder);

    json_builder_set_member_name(builder, "model");
    json_builder_add_string_value(builder, "mistral-small");

    json_builder_set_member_name(builder, "messages");
    json_builder_begin_array(builder);

    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "role");
    json_builder_add_string_value(builder, "system");
    json_builder_set_member_name(builder, "content");
    char *system_content = g_strdup_printf("User's Pidgin username: %s, status: %s, status message: %s",
                                          username, status, status_message ? status_message : "");
    json_builder_add_string_value(builder, system_content);
    json_builder_end_object(builder);
    g_free(system_content);


    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "role");
    json_builder_add_string_value(builder, "user");
    json_builder_set_member_name(builder, "content");
    json_builder_add_string_value(builder, message);
    json_builder_end_object(builder);

    json_builder_end_array(builder);

    json_builder_set_member_name(builder, "max_tokens");
    json_builder_add_int_value(builder, 1000);

    json_builder_set_member_name(builder, "temperature");
    json_builder_add_double_value(builder, 0.7);

    json_builder_end_object(builder);

    JsonNode *root = json_builder_get_root(builder);
    JsonGenerator *generator = json_generator_new();
    json_generator_set_root(generator, root);
    char *json_str = json_generator_to_data(generator, NULL);

    purple_debug_info("mistral", "Generated JSON payload: %s\n", json_str);

    json_node_free(root);
    g_object_unref(builder);
    g_object_unref(generator);

    return json_str;
}

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MistralResponse *mem = (struct MistralResponse *)userp;

    if (mem->size == 0) {
        mem->memory = malloc(1);
        if (!mem->memory) {
            purple_debug_error("mistral", "Not enough memory (malloc returned NULL)\n");
            return 0;
        }
        mem->memory[0] = '\0';
    }

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        purple_debug_error("mistral", "Not enough memory (realloc returned NULL)\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = '\0';

    purple_debug_info("mistral", "Received %zu bytes from API\n", realsize);

    return realsize;
}


static GList *mistral_status_types(PurpleAccount *account) {
    GList *types = NULL;
    PurpleStatusType *status;

    status = purple_status_type_new_full(PURPLE_STATUS_AVAILABLE, NULL, NULL, TRUE, TRUE, FALSE);
    types = g_list_append(types, status);

    status = purple_status_type_new_full(PURPLE_STATUS_OFFLINE, NULL, NULL, TRUE, TRUE, FALSE);
    types = g_list_append(types, status);

    return types;
}

static const char *mistral_list_icon(PurpleAccount *account, PurpleBuddy *buddy) {
    return "mistral-logo";
}

static PurplePluginProtocolInfo prpl_info = {
    .struct_size = sizeof(PurplePluginProtocolInfo),
    .options = OPT_PROTO_NO_PASSWORD | OPT_PROTO_IM_IMAGE,
    .icon_spec = {
        .format = "png,gif,jpeg",
        .min_width = 1,
        .min_height = 1,
        .max_width = 96,
        .max_height = 96,
        .max_filesize = 0,
        .scale_rules = PURPLE_ICON_SCALE_DISPLAY,
    },
    .list_icon = mistral_list_icon,
    .status_types = mistral_status_types,
    .login = mistral_login,
    .close = mistral_close,
    .send_im = mistral_send_im,
    .set_info = NULL,
    .send_typing = NULL,
    .get_info = NULL,
    .set_status = NULL,
    .blist_node_menu = NULL,
    .chat_info = NULL,
    .chat_info_defaults = NULL,
    .join_chat = NULL,
    .reject_chat = NULL,
    .get_chat_name = NULL,
    .chat_invite = NULL,
    .chat_leave = NULL,
    .chat_whisper = NULL,
    .chat_send = NULL,
    .keepalive = NULL,
    .register_user = NULL,
    .get_cb_info = NULL,
    .get_cb_away = NULL,
    .alias_buddy = NULL,
    .group_buddy = NULL,
    .rename_group = NULL,
    .buddy_free = NULL,
    .convo_closed = NULL,
    .normalize = NULL,
    .set_buddy_icon = NULL,
    .remove_group = NULL,
    .get_cb_real_name = NULL,
    .set_chat_topic = NULL,
    .find_blist_chat = NULL,
    .roomlist_get_list = NULL,
    .roomlist_cancel = NULL,
    .roomlist_expand_category = NULL,
    .can_receive_file = NULL,
    .send_file = NULL,
    .new_xfer = NULL,
    .offline_message = NULL,
    .whiteboard_prpl_ops = NULL,
    .send_raw = NULL,
    .roomlist_room_serialize = NULL,
    .unregister_user = NULL,
    .send_attention = NULL,
    .get_attention_types = NULL,
};

static PurplePluginInfo info = {
    .magic = PURPLE_PLUGIN_MAGIC,
    .major_version = PURPLE_MAJOR_VERSION,
    .minor_version = PURPLE_MINOR_VERSION,
    .type = PURPLE_PLUGIN_PROTOCOL,
    .flags = 0,
    .dependencies = NULL,
    .priority = PURPLE_PRIORITY_DEFAULT,

    .id = "prpl-mistral",
    .name = "Mistral AI",
    .version = "1.0",

    .summary = "Mistral AI Protocol Plugin for Pidgin",
    .description = "Chat with Mistral AI through Pidgin. Configure your API key in the account settings.",

    .author = "Your Name <your.email@domain.com>",
    .homepage = "https://github.com/yourusername/pidgin-mistral",

    .load = NULL,
    .unload = NULL,
    .destroy = NULL,

    .ui_info = NULL,
    .extra_info = &prpl_info,
    .prefs_info = NULL,
    .actions = NULL,
};

static gboolean plugin_load(PurplePlugin *plugin) {
    purple_debug_info("mistral", "Mistral AI protocol plugin loaded\n");
    return TRUE;
}

static gboolean plugin_unload(PurplePlugin *plugin) {
    purple_debug_info("mistral", "Mistral AI protocol plugin unloaded\n");
    return TRUE;
}

static void init_plugin(PurplePlugin *plugin) {
    PurpleAccountOption *option;

    option = purple_account_option_string_new("API Key", "api_key", "");
    prpl_info.protocol_options = g_list_append(prpl_info.protocol_options, option);

    info.load = plugin_load;
    info.unload = plugin_unload;
}

PURPLE_INIT_PLUGIN(mistral, init_plugin, info)
