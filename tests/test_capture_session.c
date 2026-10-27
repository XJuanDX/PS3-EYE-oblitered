/* test_capture_session.c — Edge Tests for capture_session
 *
 * Verifies critical guardrails and invariants:
 * 1. Bounded ring buffer never overflows under slow consumer (drop-oldest verified).
 * 2. Rapid start/stop cycles release resources cleanly without hanging.
 * 3. Graceful timeout handling when stopped or drained.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "capture_session.h"

static void test_timeout_when_stopped(capture_session *s)
{
    printf("[EDGE TEST 1] Testing timeout handling when stopped...\n");
    uint8_t dummy[1024];
    uint64_t qpc = 0;
    DWORD t0 = GetTickCount();
    bool res = capture_session_get_frame(s, dummy, sizeof(dummy), &qpc, 100);
    DWORD elapsed = GetTickCount() - t0;

    assert(!res);
    printf("  -> capture_session_get_frame returned false as expected (elapsed %lu ms)\n", elapsed);
    assert(elapsed < 200);
    printf("  [PASS] Test 1 passed!\n\n");
}

static void test_rapid_start_stop(capture_session *s)
{
    char err[256];
    int i;
    printf("[EDGE TEST 2] Testing rapid start/stop lifecycle (5 cycles)...\n");

    for (i = 0; i < 5; i++) {
        bool ok = capture_session_start(s, err, sizeof(err));
        if (!ok) {
            fprintf(stderr, "Cycle %d start failed: %s\n", i, err);
            assert(false);
        }
        Sleep(50);
        capture_session_stop(s);
        printf("  Cycle %d/5: started and stopped cleanly\n", i + 1);
    }
    printf("  [PASS] Test 2 passed!\n\n");
}

static void test_slow_consumer_drop_oldest(capture_session *s)
{
    char err[256];
    size_t frame_size = 640 * 480 * 2;
    uint8_t *frame_buf = (uint8_t *)malloc(frame_size);
    capture_session_stats stats;
    int received = 0;
    int target = 15;

    printf("[EDGE TEST 3] Testing slow consumer with drop-oldest policy...\n");
    assert(frame_buf != NULL);

    if (!capture_session_start(s, err, sizeof(err))) {
        fprintf(stderr, "Start failed: %s\n", err);
        assert(false);
    }

    printf("  Consumer sleeping 100 ms between frames while camera produces @ 30 fps...\n");
    while (received < target) {
        uint64_t qpc = 0;
        if (capture_session_get_frame(s, frame_buf, frame_size, &qpc, 500)) {
            received++;
            /* Verify non-empty payload */
            assert(frame_buf[0] != 0 || frame_buf[1] != 0 || frame_buf[2] != 0);
            /* Deliberate slow consumption */
            Sleep(100);
        }
    }

    capture_session_get_stats(s, &stats);
    printf("  Stats: produced=%llu, consumed=%llu, dropped_oldest=%llu\n",
           stats.produced, stats.consumed, stats.dropped_oldest);

    /* Guardrail Invariant: under slow consumer, dropped_oldest MUST be > 0 */
    assert(stats.consumed == (uint64_t)target);
    assert(stats.dropped_oldest > 0);
    assert(stats.produced >= stats.consumed + stats.dropped_oldest);

    capture_session_stop(s);
    free(frame_buf);
    printf("  [PASS] Test 3 passed! Drop-oldest confirmed without deadlocks.\n\n");
}

int main(void)
{
    capture_session s;
    char err[256];

    printf("=========================================\n");
    printf("   PS3 Eye Capture Session Guardrails    \n");
    printf("=========================================\n\n");

    printf("Initializing capture session (640x480 YUY2 @ 30fps)...\n");
    if (!capture_session_open(&s, PS3EYE_MODE_VGA_640x480, 30, err, sizeof(err))) {
        fprintf(stderr, "FATAL: capture_session_open failed: %s\n", err);
        return 1;
    }
    printf("Device and sensor initialized OK.\n\n");

    test_timeout_when_stopped(&s);
    test_rapid_start_stop(&s);
    test_slow_consumer_drop_oldest(&s);

    printf("Closing session...\n");
    capture_session_close(&s);

    printf("=========================================\n");
    printf("  ALL EDGE TESTS PASSED SUCCESSFULLY!    \n");
    printf("=========================================\n");

    return 0;
}
