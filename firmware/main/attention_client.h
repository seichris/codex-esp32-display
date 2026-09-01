#pragma once

#include "attention_model.h"
#include "esp_err.h"

esp_err_t attention_client_fetch(attention_snapshot_t *snapshot);
esp_err_t attention_client_fetch_detail(const char *thread_id, attention_detail_t *detail);
