/* ps3eye_frame.h — reassemble YUY2 frames from the OV534 bulk stream.
 *
 * Ported from the packet scanner in gspca_ov534.c (sd_pkt_scan):
 * the stream is a sequence of 2048-byte chunks, each prefixed with a 12-byte
 * UVC-style header; a frame ends on the chunk that has the EOF bit set and
 * whose accumulated payload reaches exactly width*height*2 bytes.
 */
#ifndef PS3EYE_FRAME_H
#define PS3EYE_FRAME_H

#include <stdbool.h>
#include <stdint.h>

#define PS3EYE_PAYLOAD_CHUNK 2048u
#define PS3EYE_UVCHDR_SIZE   12u

#define PS3EYE_UVC_ERR 0x40u
#define PS3EYE_UVC_PTS 0x04u
#define PS3EYE_UVC_EOF 0x02u
#define PS3EYE_UVC_FID 0x01u

typedef struct {
    uint64_t chunks;
    uint64_t bytes;
    uint64_t bad_header;
    uint64_t payload_err;
    uint64_t no_pts;
    uint64_t restarts;      /* new frame began while the previous was unfinished */
    uint64_t frames_ok;
    uint64_t frames_dropped;
    uint64_t size_mismatch;
} frame_stats;

typedef struct {
    uint32_t    expected;
    uint8_t    *buf;
    uint32_t    acc;
    bool        discard;
    uint32_t    last_pts;
    uint16_t    last_fid;
    bool        have_pts;
    frame_stats stats;
} frame_asm;

bool fa_init(frame_asm *a, uint32_t expected);
void fa_free(frame_asm *a);

/* Drop any partial frame and forget PTS/FID history. Use after a stream
 * restart so the first chunk is not mistaken for a continuation. */
void fa_reset(frame_asm *a);

/* Feed one bulk transfer. Returns true exactly when a complete frame of
 * `expected` bytes now sits in a->buf. */
bool fa_feed(frame_asm *a, const uint8_t *data, uint32_t len);

#endif /* PS3EYE_FRAME_H */
