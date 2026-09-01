#include "attention_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define RESPONSE_LIMIT (64 * 1024)

static const char *TAG = "attention_http";

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    bool overflow;
} response_buffer_t;

static esp_err_t append_response(response_buffer_t *buffer, const char *data, size_t length)
{
    if (length == 0) return ESP_OK;
    if (buffer->length + length + 1 > RESPONSE_LIMIT) {
        buffer->overflow = true;
        return ESP_ERR_NO_MEM;
    }

    const size_t required = buffer->length + length + 1;
    if (required > buffer->capacity) {
        size_t next = buffer->capacity == 0 ? 4096 : buffer->capacity;
        while (next < required) next *= 2;
        if (next > RESPONSE_LIMIT) next = RESPONSE_LIMIT;
        char *resized = realloc(buffer->data, next);
        if (resized == NULL) return ESP_ERR_NO_MEM;
        buffer->data = resized;
        buffer->capacity = next;
    }

    memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return ESP_OK;
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    response_buffer_t *buffer = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && buffer != NULL) {
        return append_response(buffer, event->data, (size_t)event->data_len);
    }
    return ESP_OK;
}

static void copy_json_string(char *destination, size_t size, const cJSON *object, const char *key)
{
    if (size == 0) return;
    destination[0] = '\0';
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsString(value) && value->valuestring != NULL) {
        strlcpy(destination, value->valuestring, size);
    }
}

static bool json_bool(const cJSON *object, const char *key)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(object, key));
}

static uint32_t json_u32(const cJSON *object, const char *key)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(value) || value->valuedouble <= 0) return 0;
    if (value->valuedouble >= UINT32_MAX) return UINT32_MAX;
    return (uint32_t)value->valuedouble;
}

static attention_status_t parse_status(const cJSON *object)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, "status");
    if (!cJSON_IsString(value) || value->valuestring == NULL) return ATTENTION_STATUS_IDLE;
    if (strcmp(value->valuestring, "running") == 0) return ATTENTION_STATUS_RUNNING;
    if (strcmp(value->valuestring, "waiting_input") == 0) return ATTENTION_STATUS_WAITING_INPUT;
    if (strcmp(value->valuestring, "waiting_approval") == 0) return ATTENTION_STATUS_WAITING_APPROVAL;
    if (strcmp(value->valuestring, "error") == 0) return ATTENTION_STATUS_ERROR;
    return ATTENTION_STATUS_IDLE;
}

static esp_err_t parse_snapshot(const char *json, attention_snapshot_t *snapshot)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) return ESP_ERR_INVALID_RESPONSE;

    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
    if (!cJSON_IsNumber(version) || version->valueint != 1 || !cJSON_IsArray(items)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->total_count = json_u32(root, "totalCount");
    snapshot->truncated = json_bool(root, "truncated");

    const cJSON *diagnostics = cJSON_GetObjectItemCaseSensitive(root, "diagnostics");
    if (cJSON_IsObject(diagnostics)) {
        snapshot->desktop_state_available = json_bool(diagnostics, "desktopStateAvailable");
        copy_json_string(snapshot->source_error, sizeof(snapshot->source_error), diagnostics, "sourceError");
    }

    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, items) {
        if (snapshot->count >= CONFIG_CODEX_ATTENTION_MAX_ITEMS) {
            snapshot->truncated = true;
            break;
        }
        if (!cJSON_IsObject(item)) continue;

        attention_item_t *output = &snapshot->items[snapshot->count];
        copy_json_string(output->title, sizeof(output->title), item, "title");
        copy_json_string(output->project, sizeof(output->project), item, "project");
        if (output->title[0] == '\0') strlcpy(output->title, "Untitled Codex thread", sizeof(output->title));
        if (output->project[0] == '\0') strlcpy(output->project, "Codex", sizeof(output->project));
        output->status = parse_status(item);
        output->age_seconds = json_u32(item, "ageSeconds");
        output->unread = json_bool(item, "unread");
        output->pinned = json_bool(item, "pinned");
        output->new_result = json_bool(item, "newResult");
        snapshot->count++;
    }

    if (snapshot->total_count == 0) snapshot->total_count = snapshot->count;
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t attention_client_fetch(attention_snapshot_t *snapshot)
{
    if (snapshot == NULL) return ESP_ERR_INVALID_ARG;

    response_buffer_t response = { 0 };
    esp_http_client_config_t config = {
        .url = CONFIG_CODEX_ATTENTION_BRIDGE_URL,
        .event_handler = http_event,
        .user_data = &response,
        .timeout_ms = 6000,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .keep_alive_enable = true,
        .user_agent = "codex-attention-display/0.1.0",
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return ESP_ERR_NO_MEM;
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_http_client_set_header(client, "Accept", "application/json");

    char authorization[320];
    if (strlen(CONFIG_CODEX_ATTENTION_BRIDGE_TOKEN) > 0) {
        int written = snprintf(
            authorization,
            sizeof(authorization),
            "Bearer %s",
            CONFIG_CODEX_ATTENTION_BRIDGE_TOKEN
        );
        if (written <= 0 || (size_t)written >= sizeof(authorization)) {
            esp_http_client_cleanup(client);
            return ESP_ERR_INVALID_SIZE;
        }
        esp_http_client_set_header(client, "Authorization", authorization);
    }

    esp_err_t result = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Request failed: %s", esp_err_to_name(result));
        free(response.data);
        return result;
    }
    if (response.overflow) {
        free(response.data);
        return ESP_ERR_INVALID_SIZE;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "Bridge returned HTTP %d", status);
        free(response.data);
        return status == 401 ? ESP_ERR_INVALID_STATE : ESP_FAIL;
    }
    if (response.data == NULL || response.length == 0) {
        free(response.data);
        return ESP_ERR_INVALID_RESPONSE;
    }

    result = parse_snapshot(response.data, snapshot);
    free(response.data);
    return result;
}
