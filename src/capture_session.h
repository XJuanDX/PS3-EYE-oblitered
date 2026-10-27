/* capture_session.h — Thread-safe capture session with bounded ring buffer.
 *
 * Decouples the low-level WinUSB reader from downstream consumers (DirectShow,
 * test harnesses, apps). Uses a bounded 3-slot ring buffer with drop-oldest
 * policy so that a slow consumer never starves or blocks USB transfers.
 */
#ifndef CAPTURE_SESSION_H
#define CAPTURE_SESSION_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

#include "ov534.h"
#include "ps3eye_usb.h"
#include "ps3eye_frame.h"

#define CAPTURE_RING_SLOTS 3

typedef struct {
    uint8_t  *data;
    uint32_t  size;
    uint64_t  timestamp_qpc;
    uint32_t  frame_index;
} capture_slot;

typedef struct {
    uint64_t produced;
    uint64_t consumed;
    uint64_t dropped_oldest;
} capture_session_stats;

typedef struct {
    ps3eye_dev              dev;
    ov534                   cam;
    ps3eye_reader          *rd;
    frame_asm               asm_;
    ps3eye_mode             mode;
    uint32_t                fps;
    uint32_t                frame_bytes;

    /* Ring buffer and sync */
    capture_slot            slots[CAPTURE_RING_SLOTS];
    int                     head;
    int                     tail;
    int                     count;
    CRITICAL_SECTION        cs;
    HANDLE                  frame_event;
    HANDLE                  stop_event;
    HANDLE                  worker_thread;

    /* State flags */
    volatile bool           is_running;
    volatile bool           device_lost;
    char                    last_error[256];

    /* Accounting */
    capture_session_stats   stats;
} capture_session;

/* Open the PS3 Eye camera and initialize the sensor. Returns false on error. */
bool capture_session_open(capture_session *s, ps3eye_mode mode, uint32_t fps, char *err, size_t errlen);

/* Start background capture worker thread. */
bool capture_session_start(capture_session *s, char *err, size_t errlen);

/* Wait for and retrieve the next available frame from the ring buffer.
 * If the ring buffer is empty, waits up to timeout_ms.
 * Returns true if a frame was copied to out_buf, false on timeout or error. */
bool capture_session_get_frame(capture_session *s, void *out_buf, size_t out_size,
                               uint64_t *timestamp_qpc, DWORD timeout_ms);

/* Stop the worker thread and camera stream cleanly. */
void capture_session_stop(capture_session *s);

/* Release all resources and close the USB handle. */
void capture_session_close(capture_session *s);

/* Query whether the device encountered a fatal hardware error / surprise removal. */
bool capture_session_is_device_lost(capture_session *s);

/* Get snapshot of current session statistics. */
void capture_session_get_stats(capture_session *s, capture_session_stats *out_stats);

#endif /* CAPTURE_SESSION_H */
