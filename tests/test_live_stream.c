#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "ps3eye_ipc.h"

int main(void)
{
    printf("Connecting to PS3EyeApp IPC producer...\n");
    ps3eye_ipc_consumer *cons = NULL;
    for (int retry = 0; retry < 50; retry++) {
        cons = ps3eye_ipc_consumer_open();
        if (cons && ps3eye_ipc_consumer_is_alive(cons)) {
            break;
        }
        if (cons) {
            ps3eye_ipc_consumer_close(cons);
            cons = NULL;
        }
        Sleep(200);
    }

    if (!cons) {
        printf("[FAIL] Could not open shared memory IPC consumer after 10s. Is PS3EyeApp running?\n");
        return 1;
    }

    if (!ps3eye_ipc_consumer_is_alive(cons)) {
        printf("[WARN] Producer opened, but marked not alive.\n");
    } else {
        printf("[PASS] Connected! Producer is ACTIVE.\n");
    }

    uint8_t *buf = (uint8_t *)malloc(PS3EYE_IPC_MAX_FRAME_BYTES);
    if (!buf) {
        ps3eye_ipc_consumer_close(cons);
        return 1;
    }

    printf("Waiting for 10 live camera frames...\n");
    int received = 0;
    for (int i = 0; i < 30; i++) {
        uint32_t w = 0, h = 0;
        uint64_t ts = 0;
        if (ps3eye_ipc_consumer_read(cons, buf, PS3EYE_IPC_MAX_FRAME_BYTES, &w, &h, &ts, 200)) {
            received++;
            printf("  Frame %d: %ux%u YUY2, ts=%llu, first bytes=[%02X %02X %02X %02X]\n",
                   received, w, h, (unsigned long long)ts, buf[0], buf[1], buf[2], buf[3]);
            if (received >= 10) break;
        } else {
            printf("  (waiting...)\n");
            Sleep(50);
        }
    }

    free(buf);
    ps3eye_ipc_consumer_close(cons);

    if (received >= 10) {
        printf("[PASS] Successfully received 10 real-time frames from PS3EyeApp!\n");
        return 0;
    } else {
        printf("[FAIL] Only received %d frames.\n", received);
        return 2;
    }
}
