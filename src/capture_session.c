/* capture_session.c — Thread-safe capture session with bounded ring buffer.
 *
 * Implements the decoupled capture core:
 * - Worker thread drains WinUSB bulk reads continuously.
 * - Complete YUY2 frames are queued in a bounded 3-slot ring buffer.
 * - Slow consumers experience oldest-frame drop without blocking USB I/O.
 * - Surprise removal / device disconnect flags the session as lost cleanly.
 */
#include "capture_session.h"
#include <stdlib.h>
#include <string.h>

static DWORD WINAPI worker_proc(LPVOID param)
{
    capture_session *s = (capture_session *)param;

    while (WaitForSingleObject(s->stop_event, 0) == WAIT_TIMEOUT) {
        uint8_t  *buf = NULL;
        uint32_t  len = 0;
        int rc = reader_wait(s->rd, &buf, &len, 100);

        if (rc == 1) {
            if (fa_feed(&s->asm_, buf, len)) {
                LARGE_INTEGER qpc;
                QueryPerformanceCounter(&qpc);

                EnterCriticalSection(&s->cs);
                if (s->count == CAPTURE_RING_SLOTS) {
                    /* Ring buffer full: consumer is behind. Drop the oldest frame. */
                    s->tail = (s->tail + 1) % CAPTURE_RING_SLOTS;
                    s->count--;
                    s->stats.dropped_oldest++;
                }

                capture_slot *slot = &s->slots[s->head];
                if (slot->data && s->asm_.buf) {
                    memcpy(slot->data, s->asm_.buf, s->frame_bytes);
                    slot->size = s->frame_bytes;
                    slot->timestamp_qpc = (uint64_t)qpc.QuadPart;
                    slot->frame_index = (uint32_t)s->stats.produced++;
                    s->head = (s->head + 1) % CAPTURE_RING_SLOTS;
                    s->count++;
                }
                LeaveCriticalSection(&s->cs);

                SetEvent(s->frame_event);
            }
            reader_release(s->rd);
        } else if (rc < 0) {
            /* Fatal error or device unplugged / surprise removal */
            EnterCriticalSection(&s->cs);
            s->device_lost = true;
            s->is_running = false;
            LeaveCriticalSection(&s->cs);
            SetEvent(s->frame_event);
            break;
        }
    }
    return 0;
}

bool capture_session_open(capture_session *s, ps3eye_mode mode, uint32_t fps, char *err, size_t errlen)
{
    int i;
    memset(s, 0, sizeof(*s));
    s->mode = mode;
    s->fps  = fps;
    s->frame_bytes = ov534_frame_bytes(mode);

    InitializeCriticalSection(&s->cs);
    s->frame_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    s->stop_event  = CreateEventW(NULL, TRUE, FALSE, NULL);

    if (!s->frame_event || !s->stop_event) {
        _snprintf_s(err, errlen, _TRUNCATE, "failed to create sync events");
        capture_session_close(s);
        return false;
    }

    if (!ps3eye_open(&s->dev, err, errlen)) {
        capture_session_close(s);
        return false;
    }

    if (!ov534_init(&s->cam, &s->dev, err, errlen)) {
        capture_session_close(s);
        return false;
    }

    for (i = 0; i < CAPTURE_RING_SLOTS; i++) {
        s->slots[i].data = (uint8_t *)malloc(s->frame_bytes);
        if (!s->slots[i].data) {
            _snprintf_s(err, errlen, _TRUNCATE, "out of memory allocating ring slots");
            capture_session_close(s);
            return false;
        }
    }

    if (!fa_init(&s->asm_, s->frame_bytes)) {
        _snprintf_s(err, errlen, _TRUNCATE, "out of memory allocating frame assembler");
        capture_session_close(s);
        return false;
    }

    s->rd = reader_create(&s->dev, 4, 65536);
    if (!s->rd) {
        _snprintf_s(err, errlen, _TRUNCATE, "failed to create USB reader");
        capture_session_close(s);
        return false;
    }

    return true;
}

bool capture_session_start(capture_session *s, char *err, size_t errlen)
{
    if (s->is_running)
        return true;

    EnterCriticalSection(&s->cs);
    s->head = 0;
    s->tail = 0;
    s->count = 0;
    s->device_lost = false;
    LeaveCriticalSection(&s->cs);

    ResetEvent(s->stop_event);
    ResetEvent(s->frame_event);
    fa_reset(&s->asm_);

    if (!ov534_start(&s->cam, s->mode, (uint8_t)s->fps, err, errlen)) {
        return false;
    }

    if (!reader_start(s->rd, err, errlen)) {
        ov534_stop(&s->cam);
        return false;
    }

    s->is_running = true;
    s->worker_thread = CreateThread(NULL, 0, worker_proc, s, 0, NULL);
    if (!s->worker_thread) {
        _snprintf_s(err, errlen, _TRUNCATE, "failed to create worker thread: %lu", GetLastError());
        reader_stop(s->rd);
        ov534_stop(&s->cam);
        s->is_running = false;
        return false;
    }

    return true;
}

bool capture_session_get_frame(capture_session *s, void *out_buf, size_t out_size,
                               uint64_t *timestamp_qpc, DWORD timeout_ms)
{
    DWORD wait_res;

    if (!s || !s->is_running || s->device_lost)
        return false;

    wait_res = WaitForSingleObject(s->frame_event, timeout_ms);
    if (wait_res != WAIT_OBJECT_0)
        return false;

    EnterCriticalSection(&s->cs);
    if (s->count == 0 || s->device_lost) {
        LeaveCriticalSection(&s->cs);
        return false;
    }

    capture_slot *slot = &s->slots[s->tail];
    if (out_size >= slot->size && out_buf && slot->data) {
        memcpy(out_buf, slot->data, slot->size);
        if (timestamp_qpc)
            *timestamp_qpc = slot->timestamp_qpc;
    }

    s->tail = (s->tail + 1) % CAPTURE_RING_SLOTS;
    s->count--;
    s->stats.consumed++;
    LeaveCriticalSection(&s->cs);

    return true;
}

void capture_session_stop(capture_session *s)
{
    if (!s || !s->is_running)
        return;

    SetEvent(s->stop_event);

    if (s->rd)
        reader_stop(s->rd);

    if (s->worker_thread) {
        WaitForSingleObject(s->worker_thread, 2000);
        CloseHandle(s->worker_thread);
        s->worker_thread = NULL;
    }

    ov534_stop(&s->cam);
    s->is_running = false;
}

void capture_session_close(capture_session *s)
{
    int i;
    if (!s) return;

    capture_session_stop(s);

    if (s->rd) {
        reader_destroy(s->rd);
        s->rd = NULL;
    }

    fa_free(&s->asm_);

    for (i = 0; i < CAPTURE_RING_SLOTS; i++) {
        if (s->slots[i].data) {
            free(s->slots[i].data);
            s->slots[i].data = NULL;
        }
    }

    ps3eye_close(&s->dev);

    if (s->frame_event) {
        CloseHandle(s->frame_event);
        s->frame_event = NULL;
    }
    if (s->stop_event) {
        CloseHandle(s->stop_event);
        s->stop_event = NULL;
    }

    DeleteCriticalSection(&s->cs);
}

bool capture_session_is_device_lost(capture_session *s)
{
    bool lost;
    if (!s) return true;
    EnterCriticalSection(&s->cs);
    lost = s->device_lost;
    LeaveCriticalSection(&s->cs);
    return lost;
}

void capture_session_get_stats(capture_session *s, capture_session_stats *out_stats)
{
    if (!s || !out_stats) return;
    EnterCriticalSection(&s->cs);
    *out_stats = s->stats;
    LeaveCriticalSection(&s->cs);
}
