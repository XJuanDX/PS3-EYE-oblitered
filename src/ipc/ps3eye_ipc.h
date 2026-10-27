/* ps3eye_ipc.h — High-performance lock-free shared memory IPC for PS3 Eye Virtual Camera.
 *
 * Implements a triple-buffered shared memory channel with Windows SDDL security
 * allowing unprivileged and sandboxed processes (Chromium/Edge/AppContainer) to
 * consume webcam frames produced by PS3EyeApp without accessing physical USB hardware.
 */
#ifndef PS3EYE_IPC_H
#define PS3EYE_IPC_H

#include <stdint.h>
#include <stdbool.h>
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS3EYE_IPC_SHM_NAME_LOCAL   L"Local\\PS3Eye_SharedMemory"
#define PS3EYE_IPC_SHM_NAME_GLOBAL  L"Global\\PS3Eye_SharedMemory"
#define PS3EYE_IPC_EVENT_NAME_LOCAL L"Local\\PS3Eye_FrameEvent"
#define PS3EYE_IPC_EVENT_NAME_GLOBAL L"Global\\PS3Eye_FrameEvent"

#define PS3EYE_SHM_MAGIC            0x59453350 // 'P3EY'
#define PS3EYE_SHM_VERSION          1
#define PS3EYE_SHM_SLOTS            3
#define PS3EYE_FRAME_HEADER_SIZE    32

#define PS3EYE_IPC_STATE_STOPPED    0
#define PS3EYE_IPC_STATE_RUNNING    1
#define PS3EYE_IPC_STATE_STOPPING   2

#define PS3EYE_IPC_MAX_WIDTH        640
#define PS3EYE_IPC_MAX_HEIGHT       480
#define PS3EYE_IPC_MAX_FRAME_BYTES  (PS3EYE_IPC_MAX_WIDTH * PS3EYE_IPC_MAX_HEIGHT * 2)

#pragma pack(push, 4)
typedef struct {
    uint32_t magic;
    uint32_t version;
    volatile uint32_t write_idx;      // Monotonically increasing write counter
    volatile uint32_t read_idx;       // Latest successfully written index
    volatile uint32_t state;          // PS3EYE_IPC_STATE_*
    uint32_t width;                   // Frame width (e.g. 640 or 320)
    uint32_t height;                  // Frame height (e.g. 480 or 240)
    uint32_t fourcc;                  // 'YUY2'
    uint32_t frame_bytes;             // width * height * 2
    uint64_t interval_100ns;          // Frame interval in 100ns units (e.g. 333333 for 30fps)
    uint32_t offsets[PS3EYE_SHM_SLOTS]; // Relative byte offsets to each frame slot
    uint32_t reserved[8];
} ps3eye_shm_header;
#pragma pack(pop)

// Opaque handles
typedef struct ps3eye_ipc_producer ps3eye_ipc_producer;
typedef struct ps3eye_ipc_consumer ps3eye_ipc_consumer;

// Producer API (called by PS3EyeApp outside sandbox)
ps3eye_ipc_producer *ps3eye_ipc_producer_create(uint32_t width, uint32_t height, uint32_t fps);
bool ps3eye_ipc_producer_write(ps3eye_ipc_producer *prod, const uint8_t *frame_data, uint32_t frame_bytes, uint64_t timestamp_100ns);
void ps3eye_ipc_producer_destroy(ps3eye_ipc_producer *prod);

// Consumer API (called by ps3eye_dshow.dll inside Chrome/Edge/OBS)
ps3eye_ipc_consumer *ps3eye_ipc_consumer_open(void);
bool ps3eye_ipc_consumer_read(ps3eye_ipc_consumer *cons, uint8_t *dst_buffer, uint32_t dst_capacity,
                              uint32_t *out_width, uint32_t *out_height, uint64_t *out_timestamp,
                              uint32_t timeout_ms);
bool ps3eye_ipc_consumer_is_alive(ps3eye_ipc_consumer *cons);
void ps3eye_ipc_consumer_close(ps3eye_ipc_consumer *cons);

#ifdef __cplusplus
}
#endif

#endif /* PS3EYE_IPC_H */
