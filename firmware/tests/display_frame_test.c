#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "display_frame.h"

static uint16_t frame[DISPLAY_FRAME_WIDTH * DISPLAY_FRAME_HEIGHT];
static uint16_t panel[DISPLAY_FRAME_WIDTH * DISPLAY_FRAME_HEIGHT];
static uint8_t staging[DISPLAY_FRAME_STRIP_BYTES];
static uint8_t queued_copy[DISPLAY_FRAME_STRIP_BYTES];
static const uint8_t *queued;
static unsigned queued_y, queued_rows, writes;
static int fail_write;
static bool fail_wait;

static int complete(void *context)
{
    (void)context;
    if (queued == NULL) return 0;
    // The async DMA source must not change before completion, even on a retry.
    assert(memcmp(queued, queued_copy, queued_rows * DISPLAY_FRAME_WIDTH * 2) == 0);
    if (fail_wait) return -2;
    for (unsigned i = 0; i < queued_rows * DISPLAY_FRAME_WIDTH; ++i) {
        panel[queued_y * DISPLAY_FRAME_WIDTH + i] = (uint16_t)((queued[i * 2] << 8) | queued[i * 2 + 1]);
    }
    queued = NULL;
    return 0;
}

static int write_strip(void *context, unsigned y, unsigned rows, const uint8_t *pixels, size_t bytes)
{
    (void)context;
    assert(queued == NULL);
    assert(y % 2 == 0 && rows % 2 == 0);
    assert(rows <= DISPLAY_FRAME_STRIP_ROWS && y + rows <= DISPLAY_FRAME_HEIGHT);
    assert(bytes == rows * DISPLAY_FRAME_WIDTH * 2 && bytes <= sizeof(staging));
    assert(y == writes * DISPLAY_FRAME_STRIP_ROWS);
    if (fail_write == (int)writes++) return -1;
    queued = pixels;
    queued_y = y;
    queued_rows = rows;
    memcpy(queued_copy, queued, bytes);
    return 0;
}

int main(void)
{
    const display_frame_io_t io = {.wait = complete, .write = write_strip};
    for (unsigned i = 0; i < sizeof(frame) / sizeof(frame[0]); ++i) frame[i] = (uint16_t)(i * 31 + 0x1234);
    memset(panel, 0xA5, sizeof(panel));
    fail_write = -1;
    assert(display_frame_send(frame, staging, &io) == 0);
    assert(writes == 26 && queued == NULL);
    assert(memcmp(panel, frame, sizeof(frame)) == 0);

    // A command/queue failure must not report a successful partial frame.
    memset(panel, 0xA5, sizeof(panel));
    writes = 0;
    fail_write = 3;
    assert(display_frame_send(frame, staging, &io) == -1);
    assert(writes == 4 && queued == NULL);
    assert(memcmp(panel, frame, sizeof(frame)) != 0);
    writes = 0;
    fail_write = -1;
    assert(display_frame_send(frame, staging, &io) == 0);
    assert(memcmp(panel, frame, sizeof(frame)) == 0);

    // Timeout leaves DMA ownership intact. Neither a new frame nor a retry
    // may change staging until the previous transfer actually completes.
    writes = 0;
    fail_wait = true;
    assert(display_frame_send(frame, staging, &io) == -2);
    assert(writes == 1 && queued != NULL);
    memset(frame, 0x3C, sizeof(frame));
    assert(display_frame_send(frame, staging, &io) == -2);
    assert(writes == 1);
    fail_wait = false;
    writes = 0;
    assert(display_frame_send(frame, staging, &io) == 0);
    assert(memcmp(panel, frame, sizeof(frame)) == 0);
    puts("Complete frame, RGB565 byte order, bounded strips, async ownership, failure and retry passed");
    return 0;
}
