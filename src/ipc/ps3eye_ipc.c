/* ps3eye_ipc.c — High-performance lock-free shared memory IPC for PS3 Eye Virtual Camera.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdlib.h>
#include <string.h>

#include "ps3eye_ipc.h"

// SDDL granting Generic All to System, Admins, Interactive Users,
// Generic Read/Write to Authenticated Users, Everyone, and ALL APPLICATION PACKAGES (AppContainer / Chromium Sandbox),
// plus Low Mandatory Integrity Level with No-Write-Up (ML;;NW;;;LW).
static const WCHAR *g_szIpcSddl =
    L"D:"
    L"(A;;GA;;;SY)"
    L"(A;;GA;;;BA)"
    L"(A;;GA;;;IU)"
    L"(A;;GRGW;;;AU)"
    L"(A;;GRGW;;;WD)"
    L"(A;;GRGW;;;AC)"
    L"S:(ML;;NW;;;LW)";

#define ALIGN_UP(val, align) (((val) + ((align) - 1)) & ~((align) - 1))

struct ps3eye_ipc_producer {
    HANDLE              hMap;
    HANDLE              hEvent;
    ps3eye_shm_header  *header;
    uint32_t            total_size;
};

struct ps3eye_ipc_consumer {
    HANDLE              hMap;
    HANDLE              hEvent;
    ps3eye_shm_header  *header;
    uint32_t            last_read_idx;
};

static SECURITY_ATTRIBUTES *CreateIpcSecurityAttributes(PSECURITY_DESCRIPTOR *outSD)
{
    SECURITY_ATTRIBUTES *pSA = (SECURITY_ATTRIBUTES *)malloc(sizeof(SECURITY_ATTRIBUTES));
    if (!pSA) return NULL;

    pSA->nLength = sizeof(SECURITY_ATTRIBUTES);
    pSA->bInheritHandle = FALSE;
    pSA->lpSecurityDescriptor = NULL;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            g_szIpcSddl, SDDL_REVISION_1, outSD, NULL))
    {
        free(pSA);
        return NULL;
    }

    pSA->lpSecurityDescriptor = *outSD;
    return pSA;
}

ps3eye_ipc_producer *ps3eye_ipc_producer_create(uint32_t width, uint32_t height, uint32_t fps)
{
    if (width == 0 || height == 0 || fps == 0) return NULL;
    if (width > PS3EYE_IPC_MAX_WIDTH || height > PS3EYE_IPC_MAX_HEIGHT) return NULL;

    ps3eye_ipc_producer *prod = (ps3eye_ipc_producer *)calloc(1, sizeof(ps3eye_ipc_producer));
    if (!prod) return NULL;

    uint32_t frame_bytes = width * height * 2;
    uint32_t slot_size = frame_bytes + PS3EYE_FRAME_HEADER_SIZE;
    slot_size = ALIGN_UP(slot_size, 32);

    uint32_t header_size = sizeof(ps3eye_shm_header);
    header_size = ALIGN_UP(header_size, 32);

    uint32_t total_size = header_size + (slot_size * PS3EYE_SHM_SLOTS);
    prod->total_size = total_size;

    PSECURITY_DESCRIPTOR pSD = NULL;
    SECURITY_ATTRIBUTES *pSA = CreateIpcSecurityAttributes(&pSD);

    // Try Local\\ first
    prod->hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, pSA, PAGE_READWRITE, 0, total_size, PS3EYE_IPC_SHM_NAME_LOCAL);
    if (!prod->hMap && GetLastError() == ERROR_ACCESS_DENIED) {
        prod->hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, total_size, PS3EYE_IPC_SHM_NAME_LOCAL);
    }

    prod->hEvent = CreateEventW(pSA, FALSE, FALSE, PS3EYE_IPC_EVENT_NAME_LOCAL);
    if (!prod->hEvent) {
        prod->hEvent = CreateEventW(NULL, FALSE, FALSE, PS3EYE_IPC_EVENT_NAME_LOCAL);
    }

    if (pSD) LocalFree(pSD);
    if (pSA) free(pSA);

    if (!prod->hMap || !prod->hEvent) {
        ps3eye_ipc_producer_destroy(prod);
        return NULL;
    }

    prod->header = (ps3eye_shm_header *)MapViewOfFile(prod->hMap, FILE_MAP_ALL_ACCESS, 0, 0, total_size);
    if (!prod->header) {
        ps3eye_ipc_producer_destroy(prod);
        return NULL;
    }

    ZeroMemory(prod->header, total_size);
    prod->header->magic = PS3EYE_SHM_MAGIC;
    prod->header->version = PS3EYE_SHM_VERSION;
    prod->header->width = width;
    prod->header->height = height;
    prod->header->fourcc = 0x32595559; // 'YUY2'
    prod->header->frame_bytes = frame_bytes;
    prod->header->interval_100ns = 10000000ULL / fps;
    prod->header->state = PS3EYE_IPC_STATE_RUNNING;
    prod->header->write_idx = 0;
    prod->header->read_idx = 0;

    for (uint32_t i = 0; i < PS3EYE_SHM_SLOTS; i++) {
        prod->header->offsets[i] = header_size + (i * slot_size);
    }

    return prod;
}

bool ps3eye_ipc_producer_write(ps3eye_ipc_producer *prod, const uint8_t *frame_data, uint32_t frame_bytes, uint64_t timestamp_100ns)
{
    if (!prod || !prod->header || !frame_data) return false;
    if (prod->header->state != PS3EYE_IPC_STATE_RUNNING) return false;
    if (frame_bytes != prod->header->frame_bytes) return false;

    // Advance write slot
    uint32_t next_idx = prod->header->write_idx + 1;
    uint32_t slot = next_idx % PS3EYE_SHM_SLOTS;
    uint32_t offset = prod->header->offsets[slot];

    uint8_t *slot_ptr = ((uint8_t *)prod->header) + offset;
    uint64_t *pTimestamp = (uint64_t *)slot_ptr;
    uint8_t *pPayload = slot_ptr + PS3EYE_FRAME_HEADER_SIZE;

    *pTimestamp = timestamp_100ns;
    memcpy(pPayload, frame_data, frame_bytes);

    MemoryBarrier();

    prod->header->write_idx = next_idx;
    prod->header->read_idx = next_idx;

    // Signal listeners that a new frame is ready
    if (prod->hEvent) {
        SetEvent(prod->hEvent);
    }

    return true;
}

void ps3eye_ipc_producer_destroy(ps3eye_ipc_producer *prod)
{
    if (!prod) return;
    if (prod->header) {
        prod->header->state = PS3EYE_IPC_STATE_STOPPED;
        UnmapViewOfFile(prod->header);
        prod->header = NULL;
    }
    if (prod->hEvent) {
        SetEvent(prod->hEvent); // Wake up any pending listeners
        CloseHandle(prod->hEvent);
        prod->hEvent = NULL;
    }
    if (prod->hMap) {
        CloseHandle(prod->hMap);
        prod->hMap = NULL;
    }
    free(prod);
}

// ---------------------------------------------------------------------------
// Consumer Implementation
// ---------------------------------------------------------------------------

ps3eye_ipc_consumer *ps3eye_ipc_consumer_open(void)
{
    ps3eye_ipc_consumer *cons = (ps3eye_ipc_consumer *)calloc(1, sizeof(ps3eye_ipc_consumer));
    if (!cons) return NULL;

    // Try opening Local namespace first, fallback to Global namespace
    cons->hMap = OpenFileMappingW(FILE_MAP_READ, FALSE, PS3EYE_IPC_SHM_NAME_LOCAL);
    if (!cons->hMap) {
        cons->hMap = OpenFileMappingW(FILE_MAP_READ, FALSE, PS3EYE_IPC_SHM_NAME_GLOBAL);
    }

    cons->hEvent = OpenEventW(SYNCHRONIZE, FALSE, PS3EYE_IPC_EVENT_NAME_LOCAL);
    if (!cons->hEvent) {
        cons->hEvent = OpenEventW(SYNCHRONIZE, FALSE, PS3EYE_IPC_EVENT_NAME_GLOBAL);
    }

    if (!cons->hMap) {
        ps3eye_ipc_consumer_close(cons);
        return NULL;
    }

    cons->header = (ps3eye_shm_header *)MapViewOfFile(cons->hMap, FILE_MAP_READ, 0, 0, 0);
    if (!cons->header || cons->header->magic != PS3EYE_SHM_MAGIC) {
        ps3eye_ipc_consumer_close(cons);
        return NULL;
    }

    cons->last_read_idx = cons->header->read_idx;
    return cons;
}

bool ps3eye_ipc_consumer_is_alive(ps3eye_ipc_consumer *cons)
{
    if (!cons || !cons->header) return false;
    return (cons->header->state == PS3EYE_IPC_STATE_RUNNING);
}

bool ps3eye_ipc_consumer_read(ps3eye_ipc_consumer *cons, uint8_t *dst_buffer, uint32_t dst_capacity,
                              uint32_t *out_width, uint32_t *out_height, uint64_t *out_timestamp,
                              uint32_t timeout_ms)
{
    if (!cons || !cons->header || !dst_buffer) return false;
    if (cons->header->state != PS3EYE_IPC_STATE_RUNNING) return false;

    // Wait for event if index has not changed
    if (cons->hEvent && cons->header->read_idx == cons->last_read_idx && timeout_ms > 0) {
        WaitForSingleObject(cons->hEvent, timeout_ms);
    }

    uint32_t current_idx = cons->header->read_idx;
    if (current_idx == 0) {
        // No frames written yet
        return false;
    }

    uint32_t slot = current_idx % PS3EYE_SHM_SLOTS;
    uint32_t offset = cons->header->offsets[slot];
    uint32_t frame_bytes = cons->header->frame_bytes;

    if (dst_capacity < frame_bytes) return false;

    uint8_t *slot_ptr = ((uint8_t *)cons->header) + offset;
    uint64_t *pTimestamp = (uint64_t *)slot_ptr;
    uint8_t *pPayload = slot_ptr + PS3EYE_FRAME_HEADER_SIZE;

    if (out_width) *out_width = cons->header->width;
    if (out_height) *out_height = cons->header->height;
    if (out_timestamp) *out_timestamp = *pTimestamp;

    memcpy(dst_buffer, pPayload, frame_bytes);
    cons->last_read_idx = current_idx;
    return true;
}

void ps3eye_ipc_consumer_close(ps3eye_ipc_consumer *cons)
{
    if (!cons) return;
    if (cons->header) {
        UnmapViewOfFile(cons->header);
        cons->header = NULL;
    }
    if (cons->hEvent) {
        CloseHandle(cons->hEvent);
        cons->hEvent = NULL;
    }
    if (cons->hMap) {
        CloseHandle(cons->hMap);
        cons->hMap = NULL;
    }
    free(cons);
}
