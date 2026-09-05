#include "display_frame.h"

int display_frame_send(const uint16_t *frame, uint8_t *staging, const display_frame_io_t *io)
{
    for (unsigned y = 0; y < DISPLAY_FRAME_HEIGHT; y += DISPLAY_FRAME_STRIP_ROWS) {
        // This includes an outstanding transfer from an earlier, timed-out frame.
        int result = io->wait(io->context);
        if (result != 0) return result;
        unsigned rows = DISPLAY_FRAME_HEIGHT - y;
        if (rows > DISPLAY_FRAME_STRIP_ROWS) rows = DISPLAY_FRAME_STRIP_ROWS;
        const size_t count = rows * DISPLAY_FRAME_WIDTH;
        for (size_t i = 0; i < count; ++i) {
            const uint16_t pixel = frame[y * DISPLAY_FRAME_WIDTH + i];
            staging[i * 2] = (uint8_t)(pixel >> 8);
            staging[i * 2 + 1] = (uint8_t)pixel;
        }
        result = io->write(io->context, y, rows, staging, count * 2);
        if (result != 0) return result;
    }
    return io->wait(io->context);
}
