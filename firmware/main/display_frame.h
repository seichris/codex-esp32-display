#pragma once
#include <stddef.h>
#include <stdint.h>

#define DISPLAY_FRAME_WIDTH 410
#define DISPLAY_FRAME_HEIGHT 502
#define DISPLAY_FRAME_STRIP_ROWS 20
#define DISPLAY_FRAME_BYTES (DISPLAY_FRAME_WIDTH * DISPLAY_FRAME_HEIGHT * 2)
#define DISPLAY_FRAME_STRIP_BYTES (DISPLAY_FRAME_WIDTH * DISPLAY_FRAME_STRIP_ROWS * 2)

// wait must finish any previous transfer before the staging memory is reused.
// All functions return zero on success; errors stop the frame immediately.
typedef struct {
    void *context;
    int (*wait)(void *context);
    int (*write)(void *context, unsigned y, unsigned rows, const uint8_t *pixels, size_t bytes);
} display_frame_io_t;

int display_frame_send(const uint16_t *frame, uint8_t *staging, const display_frame_io_t *io);
