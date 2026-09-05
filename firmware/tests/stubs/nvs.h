#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int nvs_handle_t;
typedef int esp_err_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define ESP_OK 0
static inline int nvs_open(const char *name, int mode, nvs_handle_t *handle) { return -1; }
static inline int nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value) { return -1; }
static inline int nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value) { return -1; }
static inline int nvs_commit(nvs_handle_t handle) { return -1; }
static inline void nvs_close(nvs_handle_t handle) {}
