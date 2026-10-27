/* ps3eye_frame.c — YUY2 frame reassembly from the OV534 bulk stream. */
#include <stdlib.h>
#include <string.h>

#include "ps3eye_frame.h"

bool fa_init(frame_asm *a, uint32_t expected)
{
    memset(a, 0, sizeof(*a));
    if (expected == 0)
        return false;
    a->buf = (uint8_t *)malloc(expected);
    if (!a->buf)
        return false;
    a->expected = expected;
    return true;
}

void fa_free(frame_asm *a)
{
    free(a->buf);
    a->buf = NULL;
}

void fa_reset(frame_asm *a)
{
    a->acc      = 0;
    a->discard  = true;
    a->have_pts = false;
    a->last_pts = 0;
    a->last_fid = 0;
}

static void append(frame_asm *a, const uint8_t *src, uint32_t n)
{
    if (a->acc + n > a->expected) {
        a->stats.size_mismatch++;
        a->discard = true;
        a->acc     = 0;
        return;
    }
    memcpy(a->buf + a->acc, src, n);
    a->acc += n;
}

bool fa_feed(frame_asm *a, const uint8_t *data, uint32_t len)
{
    bool     emitted = false;
    uint32_t off     = 0;

    while (off < len) {
        uint32_t       chunk = len - off;
        const uint8_t *p     = data + off;
        uint32_t       pts;
        uint16_t       fid;
        uint32_t       payload;

        if (chunk > PS3EYE_PAYLOAD_CHUNK)
            chunk = PS3EYE_PAYLOAD_CHUNK;

        a->stats.chunks++;
        a->stats.bytes += chunk;

        if (chunk < PS3EYE_UVCHDR_SIZE || p[0] != PS3EYE_UVCHDR_SIZE) {
            a->stats.bad_header++;
            a->discard = true;
            off += chunk;
            continue;
        }
        if (p[1] & PS3EYE_UVC_ERR) {
            a->stats.payload_err++;
            a->discard = true;
            off += chunk;
            continue;
        }
        if (!(p[1] & PS3EYE_UVC_PTS)) {
            a->stats.no_pts++;
            a->discard = true;
            off += chunk;
            continue;
        }

        pts = (uint32_t)p[2] | ((uint32_t)p[3] << 8) |
              ((uint32_t)p[4] << 16) | ((uint32_t)p[5] << 24);
        fid = (uint16_t)((p[1] & PS3EYE_UVC_FID) ? 1u : 0u);
        payload = chunk - PS3EYE_UVCHDR_SIZE;

        if (!a->have_pts || pts != a->last_pts || fid != a->last_fid) {
            /* A new frame starts here; anything accumulated belongs to an
             * incomplete previous frame. */
            if (a->have_pts && !a->discard && a->acc > 0) {
                a->stats.frames_dropped++;
                a->stats.restarts++;
            }
            a->have_pts = true;
            a->last_pts = pts;
            a->last_fid = fid;
            a->discard  = false;
            a->acc      = 0;
            append(a, p + PS3EYE_UVCHDR_SIZE, payload);
        } else if (p[1] & PS3EYE_UVC_EOF) {
            /* End of frame. gspca requires the exact size, and so do we. */
            if (a->discard) {
                a->stats.frames_dropped++;
            } else if (a->acc + payload != a->expected) {
                a->stats.size_mismatch++;
                a->stats.frames_dropped++;
            } else {
                memcpy(a->buf + a->acc, p + PS3EYE_UVCHDR_SIZE, payload);
                a->acc += payload;
                a->stats.frames_ok++;
                emitted = true;
            }
            a->have_pts = false;
            a->last_pts = 0;
            a->acc      = 0;
            a->discard  = true;
        } else if (!a->discard) {
            append(a, p + PS3EYE_UVCHDR_SIZE, payload);
        }

        off += chunk;
    }

    return emitted;
}
