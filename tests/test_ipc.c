/* test_ipc.c — Edge tests for PS3 Eye shared memory IPC.
 *
 * Verifies:
 *  1. Producer creation and memory mapping.
 *  2. Monotonic frame writes and payload integrity.
 *  3. Consumer opening, event synchronization, and data reading.
 *  4. Multi-consumer concurrent reads.
 *  5. Graceful shutdown / state handling.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "../src/ipc/ps3eye_ipc.h"

int main()
{
    printf("=======================================\n");
    printf("   PS3 Eye Shared Memory IPC Tests     \n");
    printf("=======================================\n\n");

    printf("1. Creating IPC producer (640x480 @ 30fps)...\n");
    ps3eye_ipc_producer *prod = ps3eye_ipc_producer_create(640, 480, 30);
    assert(prod != NULL);
    printf("  [PASS] Producer created successfully.\n\n");

    printf("2. Opening 2 independent IPC consumers simultaneously...\n");
    ps3eye_ipc_consumer *cons1 = ps3eye_ipc_consumer_open();
    ps3eye_ipc_consumer *cons2 = ps3eye_ipc_consumer_open();
    assert(cons1 != NULL);
    assert(cons2 != NULL);
    assert(ps3eye_ipc_consumer_is_alive(cons1));
    assert(ps3eye_ipc_consumer_is_alive(cons2));
    printf("  [PASS] Both consumers opened and confirmed producer is alive.\n\n");

    printf("3. Writing test pattern frame from producer...\n");
    uint32_t frame_bytes = 640 * 480 * 2;
    uint8_t *pattern = (uint8_t *)malloc(frame_bytes);
    assert(pattern != NULL);
    for (uint32_t i = 0; i < frame_bytes; i++) {
        pattern[i] = (uint8_t)(i ^ 0x5A);
    }

    uint64_t ts_in = 1234567890ULL;
    bool written = ps3eye_ipc_producer_write(prod, pattern, frame_bytes, ts_in);
    assert(written);
    printf("  [PASS] Frame written.\n\n");

    printf("4. Reading frame from consumer 1...\n");
    uint8_t *buf1 = (uint8_t *)malloc(frame_bytes);
    uint32_t w1 = 0, h1 = 0;
    uint64_t ts1 = 0;
    bool read1 = ps3eye_ipc_consumer_read(cons1, buf1, frame_bytes, &w1, &h1, &ts1, 100);
    assert(read1);
    assert(w1 == 640);
    assert(h1 == 480);
    assert(ts1 == ts_in);
    assert(memcmp(buf1, pattern, frame_bytes) == 0);
    printf("  [PASS] Consumer 1 received identical payload (640x480, ts=%llu).\n\n", ts1);

    printf("5. Reading same frame from consumer 2 (multi-client test)...\n");
    uint8_t *buf2 = (uint8_t *)malloc(frame_bytes);
    uint32_t w2 = 0, h2 = 0;
    uint64_t ts2 = 0;
    bool read2 = ps3eye_ipc_consumer_read(cons2, buf2, frame_bytes, &w2, &h2, &ts2, 100);
    assert(read2);
    assert(w2 == 640);
    assert(h2 == 480);
    assert(ts2 == ts_in);
    assert(memcmp(buf2, pattern, frame_bytes) == 0);
    printf("  [PASS] Consumer 2 received identical payload simultaneously.\n\n");

    printf("6. Testing 100 sequential frame transfers...\n");
    for (int frame = 1; frame <= 100; frame++) {
        pattern[0] = (uint8_t)frame;
        uint64_t ts = ts_in + frame;
        assert(ps3eye_ipc_producer_write(prod, pattern, frame_bytes, ts));

        assert(ps3eye_ipc_consumer_read(cons1, buf1, frame_bytes, &w1, &h1, &ts1, 50));
        assert(ts1 == ts);
        assert(buf1[0] == (uint8_t)frame);
    }
    printf("  [PASS] 100 sequential frames transferred cleanly with zero loss.\n\n");

    printf("7. Shutting down producer and verifying consumer state...\n");
    ps3eye_ipc_producer_destroy(prod);
    assert(!ps3eye_ipc_consumer_is_alive(cons1));
    printf("  [PASS] Consumer correctly detects producer shutdown.\n\n");

    ps3eye_ipc_consumer_close(cons1);
    ps3eye_ipc_consumer_close(cons2);
    free(pattern);
    free(buf1);
    free(buf2);

    printf("=======================================\n");
    printf("     ALL IPC EDGE TESTS PASSED!        \n");
    printf("=======================================\n");
    return 0;
}
