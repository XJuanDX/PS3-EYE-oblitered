/* main_gui.c — Aplicación nativa Win32 de control y productor IPC para Sony PS3 Eye.
 *
 * Características principales:
 * - Toda la interfaz y textos en Español.
 * - Renderizado en tiempo real YUY2 -> RGB32 (StretchDIBits) con consumo de CPU < 0.2% al estar oculta.
 * - Productor IPC lock-free para alimentar ps3eye_dshow.dll en Chrome/Edge/OBS/Discord.
 * - Controles deslizantes (Gain, Exposure, Brightness, Contrast, Sharpness) y automatizaciones.
 * - Soporte para minimizar a la bandeja del sistema (System Tray) y autoarranque con Windows.
 * - Reconexión automática resiliente en caso de desconexión física.
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ps3eye_ipc.h"
#include "capture_session.h"
#include "ov534.h"

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

/* Identificadores de controles UI */
#define IDC_STATUS_LABEL     1001
#define IDC_VIDEO_PREVIEW    1002
#define IDC_LABEL_COMBO      1003
#define IDC_COMBO_MODE       1004
#define IDC_CHK_AUTOSTART    1005
#define IDC_BTN_MINIMIZE     1006
#define IDC_BTN_EXIT         1007

#define IDC_GRP_SETTINGS     1010

#define IDC_LABEL_GAIN       1011
#define IDC_TRACK_GAIN       1012
#define IDC_CHK_AGC          1013

#define IDC_LABEL_EXP        1014
#define IDC_TRACK_EXP        1015
#define IDC_CHK_AEC          1016

#define IDC_LABEL_BRIGHT     1017
#define IDC_TRACK_BRIGHT     1018

#define IDC_LABEL_CONTRAST   1019
#define IDC_TRACK_CONTRAST   1020

#define IDC_LABEL_SHARP      1021
#define IDC_TRACK_SHARP      1022

#define IDC_CHK_AWB          1023
#define IDC_CHK_HFLIP        1024
#define IDC_CHK_VFLIP        1025
#define IDC_CHK_LIGHTFREQ    1026

#define WM_APP_TRAY          (WM_USER + 101)
#define WM_APP_STATUS_UPDATE (WM_USER + 102)

#define ID_TRAY_ICON         2001
#define ID_TRAY_OPEN         2002
#define ID_TRAY_AUTOSTART    2003
#define ID_TRAY_EXIT         2004

#define STATUS_DISCONNECTED  0
#define STATUS_CONNECTED     1

/* Información de modos de video soportados */
typedef struct {
    ps3eye_mode mode;
    uint32_t    fps;
    const WCHAR *label;
} video_mode_desc;

static const video_mode_desc g_modes[4] = {
    { PS3EYE_MODE_VGA_640x480,  30,  L"640x480 @ 30 FPS" },
    { PS3EYE_MODE_VGA_640x480,  60,  L"640x480 @ 60 FPS" },
    { PS3EYE_MODE_QVGA_320x240, 60,  L"320x240 @ 60 FPS" },
    { PS3EYE_MODE_QVGA_320x240, 125, L"320x240 @ 125 FPS" }
};

/* Configuración actual de cámara y controles */
typedef struct {
    int gain;        /* 0..63, defecto 20 */
    int exposure;    /* 0..255, defecto 120 */
    int brightness;  /* 0..255, defecto 0 */
    int contrast;    /* 0..255, defecto 32 */
    int sharpness;   /* 0..63, defecto 0 */
    int agc;         /* 1 (Auto Gain on) */
    int aec;         /* 1 (Auto Exposure on) */
    int awb;         /* 1 (Auto White Balance on) */
    int hflip;       /* 0 (Espejo Horizontal off) */
    int vflip;       /* 0 (Volteo Vertical off) */
    int light_freq;  /* 0 (60Hz / off), 1 (50Hz) */
    int mode_index;  /* 0..3 */
} camera_settings;

/* Contexto global de la aplicación */
typedef struct {
    HWND                 hMainWnd;
    HWND                 hVideoWnd;
    HWND                 hStatusLabel;
    HWND                 hComboMode;
    HWND                 hChkAutostart;
    HWND                 hBtnMinimize;
    HWND                 hBtnExit;

    HWND                 hTrackGain;
    HWND                 hLabelGain;
    HWND                 hChkAgc;

    HWND                 hTrackExp;
    HWND                 hLabelExp;
    HWND                 hChkAec;

    HWND                 hTrackBright;
    HWND                 hLabelBright;

    HWND                 hTrackContrast;
    HWND                 hLabelContrast;

    HWND                 hTrackSharp;
    HWND                 hLabelSharp;

    HWND                 hChkAwb;
    HWND                 hChkHFlip;
    HWND                 hChkVFlip;
    HWND                 hChkLightFreq;

    HFONT                hFont;
    HFONT                hFontBold;

    NOTIFYICONDATAW      nid;

    /* Sincronización y worker thread */
    CRITICAL_SECTION     cam_cs;
    capture_session      session;
    bool                 cam_open;
    ps3eye_ipc_producer *prod;

    HANDLE               hStopEvent;
    HANDLE               hModeChangeEvent;
    HANDLE               hWorkerThread;

    camera_settings      controls;

    /* Buffer de preview para renderizado GDI */
    CRITICAL_SECTION     preview_cs;
    uint8_t             *preview_bgrx;
    uint32_t             preview_width;
    uint32_t             preview_height;
    bool                 has_frame;
} app_context;

static app_context g_app;

/* Clamping a 8 bits */
static inline uint8_t clamp8(int v)
{
    if (v < 0)   return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

/* Conversión optimizada de YUY2 a BGRX (32-bit RGB con canal dummy) */
static void yuy2_to_bgrx(const uint8_t *src, uint8_t *dst, uint32_t width, uint32_t height)
{
    uint32_t total_pixels = width * height;
    uint32_t pairs = total_pixels >> 1;

    for (uint32_t i = 0; i < pairs; i++) {
        int y0 = src[0];
        int u  = src[1];
        int y1 = src[2];
        int v  = src[3];
        src += 4;

        int c0 = y0 - 16;
        int c1 = y1 - 16;
        int d  = u - 128;
        int e  = v - 128;

        int r0 = clamp8((298 * c0 + 409 * e + 128) >> 8);
        int g0 = clamp8((298 * c0 - 100 * d - 208 * e + 128) >> 8);
        int b0 = clamp8((298 * c0 + 516 * d + 128) >> 8);

        int r1 = clamp8((298 * c1 + 409 * e + 128) >> 8);
        int g1 = clamp8((298 * c1 - 100 * d - 208 * e + 128) >> 8);
        int b1 = clamp8((298 * c1 + 516 * d + 128) >> 8);

        dst[0] = (uint8_t)b0;
        dst[1] = (uint8_t)g0;
        dst[2] = (uint8_t)r0;
        dst[3] = 0;

        dst[4] = (uint8_t)b1;
        dst[5] = (uint8_t)g1;
        dst[6] = (uint8_t)r1;
        dst[7] = 0;

        dst += 8;
    }
}

/* Aplicar todos los controles actuales al sensor OV534/OV7720 */
static void apply_camera_controls_locked(ov534 *cam, const camera_settings *c)
{
    ov534_set_agc(cam, c->agc);
    if (!c->agc) {
        ov534_set_gain(cam, c->gain);
    }
    ov534_set_aec(cam, c->aec);
    if (!c->aec) {
        ov534_set_exposure(cam, c->exposure);
    }
    ov534_set_awb(cam, c->awb);
    ov534_set_brightness(cam, c->brightness);
    ov534_set_contrast(cam, c->contrast);
    ov534_set_sharpness(cam, c->sharpness);
    ov534_set_hvflip(cam, c->hflip, c->vflip);
    ov534_set_light_freq(cam, c->light_freq);
}

/* Consulta del estado de inicio con Windows en HKCU */
static bool IsAutoStartEnabled(void)
{
    HKEY hKey;
    LONG res = RegOpenKeyExW(HKEY_CURRENT_USER,
                             L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                             0, KEY_READ, &hKey);
    if (res != ERROR_SUCCESS)
        return false;

    WCHAR val[MAX_PATH + 32];
    DWORD valSize = sizeof(val);
    DWORD type = 0;
    res = RegQueryValueExW(hKey, L"PS3EyeCamera", NULL, &type, (LPBYTE)val, &valSize);
    RegCloseKey(hKey);

    return (res == ERROR_SUCCESS && type == REG_SZ && valSize > 0);
}

/* Establecer inicio con Windows en HKCU */
static bool SetAutoStartEnabled(bool enable)
{
    HKEY hKey;
    LONG res = RegOpenKeyExW(HKEY_CURRENT_USER,
                             L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                             0, KEY_SET_VALUE, &hKey);
    if (res != ERROR_SUCCESS)
        return false;

    if (enable) {
        WCHAR exePath[MAX_PATH];
        DWORD len = GetModuleFileNameW(NULL, exePath, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) {
            RegCloseKey(hKey);
            return false;
        }

        WCHAR cmdLine[MAX_PATH + 64];
        swprintf_s(cmdLine, sizeof(cmdLine) / sizeof(WCHAR), L"\"%s\" --minimized", exePath);
        res = RegSetValueExW(hKey, L"PS3EyeCamera", 0, REG_SZ,
                             (const BYTE *)cmdLine,
                             (DWORD)((wcslen(cmdLine) + 1) * sizeof(WCHAR)));
    } else {
        res = RegDeleteValueW(hKey, L"PS3EyeCamera");
    }

    RegCloseKey(hKey);
    return (res == ERROR_SUCCESS);
}

/* Menú contextual de la bandeja del sistema */
static void ShowTrayContextMenu(HWND hWnd)
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING, ID_TRAY_OPEN, L"Abrir Panel de Control");
    SetMenuDefaultItem(hMenu, ID_TRAY_OPEN, FALSE);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    bool autostart = IsAutoStartEnabled();
    UINT autostartFlags = MF_STRING | (autostart ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(hMenu, autostartFlags, ID_TRAY_AUTOSTART, L"Iniciar con Windows");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Salir");

    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hWnd, NULL);
    PostMessageW(hWnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

/* Hilo trabajador productor de IPC y adquisición de frames */
static DWORD WINAPI ProducerWorkerThread(LPVOID lpParam)
{
    (void)lpParam;
    uint8_t *frame_buf = (uint8_t *)malloc(PS3EYE_IPC_MAX_FRAME_BYTES);
    if (!frame_buf) return 1;

    int active_mode_index = -1;
    ps3eye_mode current_mode = PS3EYE_MODE_VGA_640x480;
    uint32_t current_fps = 30;
    uint32_t current_w = 640;
    uint32_t current_h = 480;
    uint32_t current_bytes = 640 * 480 * 2;

    while (WaitForSingleObject(g_app.hStopEvent, 0) == WAIT_TIMEOUT) {
        bool mode_changed = false;
        if (WaitForSingleObject(g_app.hModeChangeEvent, 0) == WAIT_OBJECT_0) {
            ResetEvent(g_app.hModeChangeEvent);
            mode_changed = true;
        }

        if (mode_changed || active_mode_index != g_app.controls.mode_index) {
            active_mode_index = g_app.controls.mode_index;
            current_mode = g_modes[active_mode_index].mode;
            current_fps = g_modes[active_mode_index].fps;
            current_w = ov534_width(current_mode);
            current_h = ov534_height(current_mode);
            current_bytes = ov534_frame_bytes(current_mode);

            EnterCriticalSection(&g_app.cam_cs);
            if (g_app.cam_open) {
                capture_session_stop(&g_app.session);
                capture_session_close(&g_app.session);
                g_app.cam_open = false;
            }
            if (g_app.prod) {
                ps3eye_ipc_producer_destroy(g_app.prod);
                g_app.prod = NULL;
            }
            LeaveCriticalSection(&g_app.cam_cs);
        }

        if (!g_app.cam_open) {
            PostMessageW(g_app.hMainWnd, WM_APP_STATUS_UPDATE, STATUS_DISCONNECTED, 0);

            char err[256] = {0};
            EnterCriticalSection(&g_app.cam_cs);
            bool ok = capture_session_open(&g_app.session, current_mode, current_fps, err, sizeof(err));
            if (ok) {
                apply_camera_controls_locked(&g_app.session.cam, &g_app.controls);
                g_app.cam_open = true;
                g_app.prod = ps3eye_ipc_producer_create(current_w, current_h, current_fps);
            }
            LeaveCriticalSection(&g_app.cam_cs);

            if (!ok || !g_app.cam_open) {
                HANDLE hEvents[2] = { g_app.hStopEvent, g_app.hModeChangeEvent };
                WaitForMultipleObjects(2, hEvents, FALSE, 2000);
                continue;
            }

            PostMessageW(g_app.hMainWnd, WM_APP_STATUS_UPDATE, STATUS_CONNECTED, (LPARAM)active_mode_index);
        }

        /* Streaming bajo demanda: encender sensor y LED solo si hay clientes o preview activa */
        bool preview_active = (g_app.hMainWnd && IsWindowVisible(g_app.hMainWnd) && !IsIconic(g_app.hMainWnd));
        bool client_active = ps3eye_ipc_producer_has_active_consumers(g_app.prod);
        bool need_streaming = preview_active || client_active;

        if (!need_streaming) {
            if (g_app.session.is_running) {
                EnterCriticalSection(&g_app.cam_cs);
                capture_session_stop(&g_app.session);
                LeaveCriticalSection(&g_app.cam_cs);
            }
            Sleep(100);
            continue;
        }

        if (!g_app.session.is_running) {
            char start_err[256] = {0};
            EnterCriticalSection(&g_app.cam_cs);
            bool started = capture_session_start(&g_app.session, start_err, sizeof(start_err));
            LeaveCriticalSection(&g_app.cam_cs);
            if (!started) {
                Sleep(200);
                continue;
            }
        }

        uint64_t qpc_ts = 0;
        bool got = capture_session_get_frame(&g_app.session, frame_buf, current_bytes, &qpc_ts, 100);
        if (!got) {
            if (capture_session_is_device_lost(&g_app.session)) {
                EnterCriticalSection(&g_app.cam_cs);
                capture_session_stop(&g_app.session);
                capture_session_close(&g_app.session);
                g_app.cam_open = false;
                if (g_app.prod) {
                    ps3eye_ipc_producer_destroy(g_app.prod);
                    g_app.prod = NULL;
                }
                LeaveCriticalSection(&g_app.cam_cs);

                PostMessageW(g_app.hMainWnd, WM_APP_STATUS_UPDATE, STATUS_DISCONNECTED, 0);

                HANDLE hEvents[2] = { g_app.hStopEvent, g_app.hModeChangeEvent };
                WaitForMultipleObjects(2, hEvents, FALSE, 2000);
            }
            continue;
        }

        /* Escribir al canal de memoria compartida IPC con timestamp en unidades de 100ns */
        FILETIME ft;
        GetSystemTimePreciseAsFileTime(&ft);
        uint64_t ts_100ns = (((uint64_t)ft.dwHighDateTime) << 32) | ft.dwLowDateTime;

        if (g_app.prod) {
            ps3eye_ipc_producer_write(g_app.prod, frame_buf, current_bytes, ts_100ns);
        }

        /* Si la ventana está minimizada u oculta en el tray, omitir la conversión
         * y el renderizado para mantener el consumo de CPU por debajo del 0.2%. */
        if (g_app.hMainWnd && IsWindowVisible(g_app.hMainWnd) && !IsIconic(g_app.hMainWnd)) {
            EnterCriticalSection(&g_app.preview_cs);
            yuy2_to_bgrx(frame_buf, g_app.preview_bgrx, current_w, current_h);
            g_app.preview_width = current_w;
            g_app.preview_height = current_h;
            g_app.has_frame = true;
            LeaveCriticalSection(&g_app.preview_cs);

            if (g_app.hVideoWnd) {
                InvalidateRect(g_app.hVideoWnd, NULL, FALSE);
            }
        }
    }

    EnterCriticalSection(&g_app.cam_cs);
    if (g_app.cam_open) {
        capture_session_stop(&g_app.session);
        capture_session_close(&g_app.session);
        g_app.cam_open = false;
    }
    if (g_app.prod) {
        ps3eye_ipc_producer_destroy(g_app.prod);
        g_app.prod = NULL;
    }
    LeaveCriticalSection(&g_app.cam_cs);

    free(frame_buf);
    return 0;
}

/* Procedimiento de ventana para el área de vista previa de video */
static LRESULT CALLBACK VideoWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);

            EnterCriticalSection(&g_app.preview_cs);
            if (g_app.has_frame && g_app.preview_bgrx && g_app.preview_width > 0 && g_app.preview_height > 0) {
                BITMAPINFO bmi;
                memset(&bmi, 0, sizeof(bmi));
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = (LONG)g_app.preview_width;
                bmi.bmiHeader.biHeight = -(LONG)g_app.preview_height; /* Top-down DIB */
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;

                SetStretchBltMode(hdc, COLORONCOLOR);
                StretchDIBits(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                              0, 0, g_app.preview_width, g_app.preview_height,
                              g_app.preview_bgrx, &bmi, DIB_RGB_COLORS, SRCCOPY);
            } else {
                HBRUSH hBrush = CreateSolidBrush(RGB(20, 20, 24));
                FillRect(hdc, &rc, hBrush);
                DeleteObject(hBrush);

                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(239, 68, 68));
                SelectObject(hdc, g_app.hFontBold);
                DrawTextW(hdc, L"Cámara desconectada o no detectada", -1, &rc,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            LeaveCriticalSection(&g_app.preview_cs);

            EndPaint(hWnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

/* Helper para actualizar el texto de etiquetas */
static void UpdateLabel(HWND hLabel, const WCHAR *format, int val)
{
    WCHAR buf[64];
    swprintf_s(buf, sizeof(buf) / sizeof(WCHAR), format, val);
    SetWindowTextW(hLabel, buf);
}

/* Procedimiento de la ventana principal */
static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_CREATE: {
            g_app.hMainWnd = hWnd;

            /* Tipografías del sistema Segoe UI */
            g_app.hFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            g_app.hFontBold = CreateFontW(-12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            /* 1. Etiqueta de estado */
            g_app.hStatusLabel = CreateWindowW(L"STATIC", L"Estado: Buscando cámara...",
                                               WS_CHILD | WS_VISIBLE | SS_LEFT,
                                               20, 15, 400, 20, hWnd, (HMENU)IDC_STATUS_LABEL, NULL, NULL);
            SendMessageW(g_app.hStatusLabel, WM_SETFONT, (WPARAM)g_app.hFontBold, TRUE);

            /* 2. Área de Vista Previa de Video */
            g_app.hVideoWnd = CreateWindowExW(WS_EX_CLIENTEDGE, L"PS3EyeVideoPreviewClass", NULL,
                                              WS_CHILD | WS_VISIBLE,
                                              20, 40, 400, 300, hWnd, (HMENU)IDC_VIDEO_PREVIEW, NULL, NULL);

            /* 3. Selector de Resolución / FPS */
            HWND hLblCombo = CreateWindowW(L"STATIC", L"Resolución / FPS:",
                                           WS_CHILD | WS_VISIBLE | SS_LEFT,
                                           20, 355, 120, 20, hWnd, (HMENU)IDC_LABEL_COMBO, NULL, NULL);
            SendMessageW(hLblCombo, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);

            g_app.hComboMode = CreateWindowW(L"COMBOBOX", NULL,
                                             WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                             145, 352, 275, 200, hWnd, (HMENU)IDC_COMBO_MODE, NULL, NULL);
            SendMessageW(g_app.hComboMode, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);

            for (int i = 0; i < 4; i++) {
                SendMessageW(g_app.hComboMode, CB_ADDSTRING, 0, (LPARAM)g_modes[i].label);
            }
            SendMessageW(g_app.hComboMode, CB_SETCURSEL, g_app.controls.mode_index, 0);

            /* 4. Checkbox "Iniciar con Windows" */
            g_app.hChkAutostart = CreateWindowW(L"BUTTON", L"Iniciar con Windows",
                                                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                                20, 395, 200, 24, hWnd, (HMENU)IDC_CHK_AUTOSTART, NULL, NULL);
            SendMessageW(g_app.hChkAutostart, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);
            if (IsAutoStartEnabled()) {
                CheckDlgButton(hWnd, IDC_CHK_AUTOSTART, BST_CHECKED);
            }

            /* 5. Botones de acción */
            g_app.hBtnMinimize = CreateWindowW(L"BUTTON", L"Minimizar a la bandeja",
                                               WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                               20, 440, 190, 34, hWnd, (HMENU)IDC_BTN_MINIMIZE, NULL, NULL);
            SendMessageW(g_app.hBtnMinimize, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);

            g_app.hBtnExit = CreateWindowW(L"BUTTON", L"Salir",
                                           WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           225, 440, 195, 34, hWnd, (HMENU)IDC_BTN_EXIT, NULL, NULL);
            SendMessageW(g_app.hBtnExit, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);

            /* 6. Panel GroupBox "Ajustes de Imagen y Sensor" */
            HWND hGroup = CreateWindowW(L"BUTTON", L"Ajustes de Sensor e Imagen",
                                        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                        440, 10, 400, 465, hWnd, (HMENU)IDC_GRP_SETTINGS, NULL, NULL);
            SendMessageW(hGroup, WM_SETFONT, (WPARAM)g_app.hFontBold, TRUE);

            /* --- Ganancia --- */
            g_app.hLabelGain = CreateWindowW(L"STATIC", L"Ganancia: 20",
                                             WS_CHILD | WS_VISIBLE | SS_LEFT,
                                             455, 35, 150, 18, hWnd, (HMENU)IDC_LABEL_GAIN, NULL, NULL);
            SendMessageW(g_app.hLabelGain, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);

            g_app.hChkAgc = CreateWindowW(L"BUTTON", L"Auto Ganancia",
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                          700, 32, 125, 22, hWnd, (HMENU)IDC_CHK_AGC, NULL, NULL);
            SendMessageW(g_app.hChkAgc, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);
            CheckDlgButton(hWnd, IDC_CHK_AGC, g_app.controls.agc ? BST_CHECKED : BST_UNCHECKED);

            g_app.hTrackGain = CreateWindowW(TRACKBAR_CLASSW, NULL,
                                             WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
                                             455, 55, 370, 30, hWnd, (HMENU)IDC_TRACK_GAIN, NULL, NULL);
            SendMessageW(g_app.hTrackGain, TBM_SETRANGE, TRUE, MAKELPARAM(0, 63));
            SendMessageW(g_app.hTrackGain, TBM_SETPOS, TRUE, g_app.controls.gain);
            EnableWindow(g_app.hTrackGain, !g_app.controls.agc);

            /* --- Exposición --- */
            g_app.hLabelExp = CreateWindowW(L"STATIC", L"Exposición: 120",
                                            WS_CHILD | WS_VISIBLE | SS_LEFT,
                                            455, 95, 150, 18, hWnd, (HMENU)IDC_LABEL_EXP, NULL, NULL);
            SendMessageW(g_app.hLabelExp, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);

            g_app.hChkAec = CreateWindowW(L"BUTTON", L"Auto Exposición",
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                          700, 92, 125, 22, hWnd, (HMENU)IDC_CHK_AEC, NULL, NULL);
            SendMessageW(g_app.hChkAec, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);
            CheckDlgButton(hWnd, IDC_CHK_AEC, g_app.controls.aec ? BST_CHECKED : BST_UNCHECKED);

            g_app.hTrackExp = CreateWindowW(TRACKBAR_CLASSW, NULL,
                                            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
                                            455, 115, 370, 30, hWnd, (HMENU)IDC_TRACK_EXP, NULL, NULL);
            SendMessageW(g_app.hTrackExp, TBM_SETRANGE, TRUE, MAKELPARAM(0, 255));
            SendMessageW(g_app.hTrackExp, TBM_SETPOS, TRUE, g_app.controls.exposure);
            EnableWindow(g_app.hTrackExp, !g_app.controls.aec);

            /* --- Brillo --- */
            g_app.hLabelBright = CreateWindowW(L"STATIC", L"Brillo: 0",
                                               WS_CHILD | WS_VISIBLE | SS_LEFT,
                                               455, 155, 150, 18, hWnd, (HMENU)IDC_LABEL_BRIGHT, NULL, NULL);
            SendMessageW(g_app.hLabelBright, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);

            g_app.hTrackBright = CreateWindowW(TRACKBAR_CLASSW, NULL,
                                               WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
                                               455, 175, 370, 30, hWnd, (HMENU)IDC_TRACK_BRIGHT, NULL, NULL);
            SendMessageW(g_app.hTrackBright, TBM_SETRANGE, TRUE, MAKELPARAM(0, 255));
            SendMessageW(g_app.hTrackBright, TBM_SETPOS, TRUE, g_app.controls.brightness);

            /* --- Contraste --- */
            g_app.hLabelContrast = CreateWindowW(L"STATIC", L"Contraste: 32",
                                                 WS_CHILD | WS_VISIBLE | SS_LEFT,
                                                 455, 215, 150, 18, hWnd, (HMENU)IDC_LABEL_CONTRAST, NULL, NULL);
            SendMessageW(g_app.hLabelContrast, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);

            g_app.hTrackContrast = CreateWindowW(TRACKBAR_CLASSW, NULL,
                                                 WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
                                                 455, 235, 370, 30, hWnd, (HMENU)IDC_TRACK_CONTRAST, NULL, NULL);
            SendMessageW(g_app.hTrackContrast, TBM_SETRANGE, TRUE, MAKELPARAM(0, 255));
            SendMessageW(g_app.hTrackContrast, TBM_SETPOS, TRUE, g_app.controls.contrast);

            /* --- Nitidez --- */
            g_app.hLabelSharp = CreateWindowW(L"STATIC", L"Nitidez: 0",
                                              WS_CHILD | WS_VISIBLE | SS_LEFT,
                                              455, 275, 150, 18, hWnd, (HMENU)IDC_LABEL_SHARP, NULL, NULL);
            SendMessageW(g_app.hLabelSharp, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);

            g_app.hTrackSharp = CreateWindowW(TRACKBAR_CLASSW, NULL,
                                              WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
                                              455, 295, 370, 30, hWnd, (HMENU)IDC_TRACK_SHARP, NULL, NULL);
            SendMessageW(g_app.hTrackSharp, TBM_SETRANGE, TRUE, MAKELPARAM(0, 63));
            SendMessageW(g_app.hTrackSharp, TBM_SETPOS, TRUE, g_app.controls.sharpness);

            /* --- Checkboxes adicionales --- */
            g_app.hChkAwb = CreateWindowW(L"BUTTON", L"Auto Balance de Blancos",
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                          455, 340, 240, 22, hWnd, (HMENU)IDC_CHK_AWB, NULL, NULL);
            SendMessageW(g_app.hChkAwb, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);
            CheckDlgButton(hWnd, IDC_CHK_AWB, g_app.controls.awb ? BST_CHECKED : BST_UNCHECKED);

            g_app.hChkHFlip = CreateWindowW(L"BUTTON", L"Espejo Horizontal",
                                            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                            455, 370, 240, 22, hWnd, (HMENU)IDC_CHK_HFLIP, NULL, NULL);
            SendMessageW(g_app.hChkHFlip, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);
            CheckDlgButton(hWnd, IDC_CHK_HFLIP, g_app.controls.hflip ? BST_CHECKED : BST_UNCHECKED);

            g_app.hChkVFlip = CreateWindowW(L"BUTTON", L"Volteo Vertical",
                                            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                            455, 400, 240, 22, hWnd, (HMENU)IDC_CHK_VFLIP, NULL, NULL);
            SendMessageW(g_app.hChkVFlip, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);
            CheckDlgButton(hWnd, IDC_CHK_VFLIP, g_app.controls.vflip ? BST_CHECKED : BST_UNCHECKED);

            g_app.hChkLightFreq = CreateWindowW(L"BUTTON", L"Anti-parpadeo 50Hz",
                                                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                                455, 430, 240, 22, hWnd, (HMENU)IDC_CHK_LIGHTFREQ, NULL, NULL);
            SendMessageW(g_app.hChkLightFreq, WM_SETFONT, (WPARAM)g_app.hFont, TRUE);
            CheckDlgButton(hWnd, IDC_CHK_LIGHTFREQ, g_app.controls.light_freq ? BST_CHECKED : BST_UNCHECKED);

            /* Icono en la bandeja del sistema (System Tray) */
            memset(&g_app.nid, 0, sizeof(g_app.nid));
            g_app.nid.cbSize = sizeof(NOTIFYICONDATAW);
            g_app.nid.hWnd = hWnd;
            g_app.nid.uID = ID_TRAY_ICON;
            g_app.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            g_app.nid.uCallbackMessage = WM_APP_TRAY;
            g_app.nid.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
            wcscpy_s(g_app.nid.szTip, sizeof(g_app.nid.szTip) / sizeof(WCHAR), L"PS3 EYE oblitered");
            Shell_NotifyIconW(NIM_ADD, &g_app.nid);

            return 0;
        }

        case WM_HSCROLL: {
            HWND hTrack = (HWND)lParam;
            int val = (int)SendMessageW(hTrack, TBM_GETPOS, 0, 0);

            EnterCriticalSection(&g_app.cam_cs);
            if (hTrack == g_app.hTrackGain) {
                g_app.controls.gain = val;
                UpdateLabel(g_app.hLabelGain, L"Ganancia: %d", val);
                if (g_app.cam_open && !g_app.controls.agc) {
                    ov534_set_gain(&g_app.session.cam, val);
                }
            } else if (hTrack == g_app.hTrackExp) {
                g_app.controls.exposure = val;
                UpdateLabel(g_app.hLabelExp, L"Exposición: %d", val);
                if (g_app.cam_open && !g_app.controls.aec) {
                    ov534_set_exposure(&g_app.session.cam, val);
                }
            } else if (hTrack == g_app.hTrackBright) {
                g_app.controls.brightness = val;
                UpdateLabel(g_app.hLabelBright, L"Brillo: %d", val);
                if (g_app.cam_open) {
                    ov534_set_brightness(&g_app.session.cam, val);
                }
            } else if (hTrack == g_app.hTrackContrast) {
                g_app.controls.contrast = val;
                UpdateLabel(g_app.hLabelContrast, L"Contraste: %d", val);
                if (g_app.cam_open) {
                    ov534_set_contrast(&g_app.session.cam, val);
                }
            } else if (hTrack == g_app.hTrackSharp) {
                g_app.controls.sharpness = val;
                UpdateLabel(g_app.hLabelSharp, L"Nitidez: %d", val);
                if (g_app.cam_open) {
                    ov534_set_sharpness(&g_app.session.cam, val);
                }
            }
            LeaveCriticalSection(&g_app.cam_cs);
            return 0;
        }

        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            WORD code = HIWORD(wParam);

            if (code == BN_CLICKED) {
                bool checked = (IsDlgButtonChecked(hWnd, id) == BST_CHECKED);
                EnterCriticalSection(&g_app.cam_cs);
                switch (id) {
                    case IDC_CHK_AGC:
                        g_app.controls.agc = checked ? 1 : 0;
                        EnableWindow(g_app.hTrackGain, !checked);
                        if (g_app.cam_open) {
                            ov534_set_agc(&g_app.session.cam, g_app.controls.agc);
                            if (!checked) {
                                ov534_set_gain(&g_app.session.cam, g_app.controls.gain);
                            }
                        }
                        break;
                    case IDC_CHK_AEC:
                        g_app.controls.aec = checked ? 1 : 0;
                        EnableWindow(g_app.hTrackExp, !checked);
                        if (g_app.cam_open) {
                            ov534_set_aec(&g_app.session.cam, g_app.controls.aec);
                            if (!checked) {
                                ov534_set_exposure(&g_app.session.cam, g_app.controls.exposure);
                            }
                        }
                        break;
                    case IDC_CHK_AWB:
                        g_app.controls.awb = checked ? 1 : 0;
                        if (g_app.cam_open) {
                            ov534_set_awb(&g_app.session.cam, g_app.controls.awb);
                        }
                        break;
                    case IDC_CHK_HFLIP:
                        g_app.controls.hflip = checked ? 1 : 0;
                        if (g_app.cam_open) {
                            ov534_set_hvflip(&g_app.session.cam, g_app.controls.hflip, g_app.controls.vflip);
                        }
                        break;
                    case IDC_CHK_VFLIP:
                        g_app.controls.vflip = checked ? 1 : 0;
                        if (g_app.cam_open) {
                            ov534_set_hvflip(&g_app.session.cam, g_app.controls.hflip, g_app.controls.vflip);
                        }
                        break;
                    case IDC_CHK_LIGHTFREQ:
                        g_app.controls.light_freq = checked ? 1 : 0;
                        if (g_app.cam_open) {
                            ov534_set_light_freq(&g_app.session.cam, g_app.controls.light_freq);
                        }
                        break;
                    case IDC_CHK_AUTOSTART:
                        SetAutoStartEnabled(checked);
                        break;
                    case IDC_BTN_MINIMIZE:
                        ShowWindow(hWnd, SW_HIDE);
                        break;
                    case IDC_BTN_EXIT:
                        DestroyWindow(hWnd);
                        break;
                }
                LeaveCriticalSection(&g_app.cam_cs);
            } else if (code == CBN_SELCHANGE && id == IDC_COMBO_MODE) {
                int sel = (int)SendMessageW(g_app.hComboMode, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel <= 3 && sel != g_app.controls.mode_index) {
                    g_app.controls.mode_index = sel;
                    SetEvent(g_app.hModeChangeEvent);
                }
            }
            return 0;
        }

        case WM_APP_STATUS_UPDATE: {
            if (wParam == STATUS_CONNECTED) {
                int idx = (int)lParam;
                WCHAR text[128];
                swprintf_s(text, sizeof(text) / sizeof(WCHAR),
                           L"Estado: Conectada (%s)", g_modes[idx].label);
                SetWindowTextW(g_app.hStatusLabel, text);
            } else {
                SetWindowTextW(g_app.hStatusLabel, L"Estado: Cámara desconectada o no detectada");
                EnterCriticalSection(&g_app.preview_cs);
                g_app.has_frame = false;
                LeaveCriticalSection(&g_app.preview_cs);
                if (g_app.hVideoWnd) {
                    InvalidateRect(g_app.hVideoWnd, NULL, FALSE);
                }
            }
            return 0;
        }

        case WM_APP_TRAY: {
            if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
                ShowWindow(hWnd, SW_RESTORE);
                SetForegroundWindow(hWnd);
            } else if (LOWORD(lParam) == WM_RBUTTONUP) {
                ShowTrayContextMenu(hWnd);
            }
            return 0;
        }

        case WM_SYSCOMMAND: {
            if ((wParam & 0xFFF0) == SC_MINIMIZE) {
                ShowWindow(hWnd, SW_HIDE);
                return 0;
            }
            break;
        }

        case WM_CLOSE: {
            /* Al cerrar la ventana [X], se oculta al System Tray para no interrumpir streams */
            ShowWindow(hWnd, SW_HIDE);
            return 0;
        }

        case WM_DESTROY: {
            Shell_NotifyIconW(NIM_DELETE, &g_app.nid);

            if (g_app.hStopEvent) {
                SetEvent(g_app.hStopEvent);
            }

            if (g_app.hWorkerThread) {
                WaitForSingleObject(g_app.hWorkerThread, 3000);
                CloseHandle(g_app.hWorkerThread);
                g_app.hWorkerThread = NULL;
            }

            if (g_app.hFont) DeleteObject(g_app.hFont);
            if (g_app.hFontBold) DeleteObject(g_app.hFontBold);

            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

/* Punto de entrada WinMain */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;

    /* Control de instancia única */
    HANDLE hSingleMutex = CreateMutexW(NULL, FALSE, L"Local\\PS3EyeOblitered_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExistingWnd = FindWindowW(L"PS3EyeMainWindowClass", NULL);
        if (hExistingWnd) {
            ShowWindow(hExistingWnd, SW_SHOW);
            ShowWindow(hExistingWnd, SW_RESTORE);
            SetForegroundWindow(hExistingWnd);
        }
        if (hSingleMutex) CloseHandle(hSingleMutex);
        return 0;
    }

    /* Inicializar Common Controls para estilos visuales modernos */
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);

    /* Inicializar contexto y configuraciones por defecto */
    memset(&g_app, 0, sizeof(g_app));
    InitializeCriticalSection(&g_app.cam_cs);
    InitializeCriticalSection(&g_app.preview_cs);

    g_app.controls.gain       = 20;   /* Defecto: 20 (0..63) */
    g_app.controls.exposure   = 120;  /* Defecto: 120 (0..255) */
    g_app.controls.brightness = 0;    /* Defecto: 0 (0..255) */
    g_app.controls.contrast   = 32;   /* Defecto: 32 (0..255) */
    g_app.controls.sharpness  = 0;    /* Defecto: 0 (0..63) */
    g_app.controls.agc        = 1;    /* Auto Ganancia activa */
    g_app.controls.aec        = 1;    /* Auto Exposición activa */
    g_app.controls.awb        = 1;    /* Auto Balance de Blancos activo */
    g_app.controls.hflip      = 0;
    g_app.controls.vflip      = 0;
    g_app.controls.light_freq = 0;    /* 60 Hz / off */
    g_app.controls.mode_index = 0;    /* 640x480 @ 30 FPS */

    g_app.preview_bgrx = (uint8_t *)malloc(PS3EYE_IPC_MAX_WIDTH * PS3EYE_IPC_MAX_HEIGHT * 4);
    if (!g_app.preview_bgrx) return 1;

    g_app.hStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_app.hModeChangeEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    /* Registro de clases de ventana */
    WNDCLASSEXW wcPreview;
    memset(&wcPreview, 0, sizeof(wcPreview));
    wcPreview.cbSize        = sizeof(WNDCLASSEXW);
    wcPreview.lpfnWndProc   = VideoWndProc;
    wcPreview.hInstance     = hInstance;
    wcPreview.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wcPreview.lpszClassName = L"PS3EyeVideoPreviewClass";
    RegisterClassExW(&wcPreview);

    WNDCLASSEXW wcMain;
    memset(&wcMain, 0, sizeof(wcMain));
    wcMain.cbSize        = sizeof(WNDCLASSEXW);
    wcMain.lpfnWndProc   = MainWndProc;
    wcMain.hInstance     = hInstance;
    wcMain.hIcon         = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wcMain.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wcMain.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcMain.lpszClassName = L"PS3EyeMainWindowClass";
    RegisterClassExW(&wcMain);

    /* Cálculo del tamaño de ventana con estilo no redimensionable */
    DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT wr = { 0, 0, 860, 495 };
    AdjustWindowRect(&wr, dwStyle, FALSE);

    HWND hWnd = CreateWindowExW(0, L"PS3EyeMainWindowClass", L"PS3 EYE oblitered",
                                dwStyle,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                wr.right - wr.left, wr.bottom - wr.top,
                                NULL, NULL, hInstance, NULL);

    if (!hWnd) return 1;

    /* Iniciar hilo trabajador de adquisición y productor IPC */
    g_app.hWorkerThread = CreateThread(NULL, 0, ProducerWorkerThread, NULL, 0, NULL);

    /* Comprobar argumentos de línea de comandos para inicio minimizado */
    LPWSTR lpWideCmdLine = GetCommandLineW();
    bool startMinimized = (lpWideCmdLine &&
                           (wcsstr(lpWideCmdLine, L"--minimized") != NULL ||
                            wcsstr(lpWideCmdLine, L"/tray") != NULL ||
                            wcsstr(lpWideCmdLine, L"-minimized") != NULL));

    if (!startMinimized) {
        ShowWindow(hWnd, nCmdShow);
        UpdateWindow(hWnd);
    } else {
        ShowWindow(hWnd, SW_HIDE);
    }

    /* Bucle de mensajes */
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_app.preview_bgrx) {
        free(g_app.preview_bgrx);
        g_app.preview_bgrx = NULL;
    }

    if (g_app.hStopEvent) CloseHandle(g_app.hStopEvent);
    if (g_app.hModeChangeEvent) CloseHandle(g_app.hModeChangeEvent);
    DeleteCriticalSection(&g_app.cam_cs);
    DeleteCriticalSection(&g_app.preview_cs);

    return (int)msg.wParam;
}
