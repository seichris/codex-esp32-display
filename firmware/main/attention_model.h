#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"

#define ATTENTION_TITLE_MAX 97
#define ATTENTION_PROJECT_MAX 49
#define ATTENTION_ERROR_MAX 161

typedef enum {
    ATTENTION_STATUS_IDLE = 0,
    ATTENTION_STATUS_RUNNING,
    ATTENTION_STATUS_WAITING_INPUT,
    ATTENTION_STATUS_WAITING_APPROVAL,
    ATTENTION_STATUS_ERROR,
} attention_status_t;

typedef struct {
    char title[ATTENTION_TITLE_MAX];
    char project[ATTENTION_PROJECT_MAX];
    attention_status_t status;
    uint32_t age_seconds;
    bool unread;
    bool pinned;
    bool new_result;
} attention_item_t;

typedef struct {
    uint32_t count;
    uint32_t total_count;
    bool truncated;
    bool desktop_state_available;
    char source_error[ATTENTION_ERROR_MAX];
    attention_item_t items[CONFIG_CODEX_ATTENTION_MAX_ITEMS];
} attention_snapshot_t;
