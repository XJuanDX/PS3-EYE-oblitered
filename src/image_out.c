/* image_out.c — PPM writer and directory helper. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "image_out.h"

bool ensure_dir(const char *path)
{
    char  tmp[1024];
    size_t n, i;

    if (!path || !*path)
        return false;

    n = strlen(path);
    if (n >= sizeof(tmp))
        return false;
    memcpy(tmp, path, n + 1);

    for (i = 0; i < n; i++) {
        if (tmp[i] == '/' || tmp[i] == '\\') {
            char saved = tmp[i];
            tmp[i] = '\0';
            if (tmp[0])
                CreateDirectoryA(tmp, NULL);
            tmp[i] = saved;
        }
    }
    if (!CreateDirectoryA(tmp, NULL)) {
        DWORD e = GetLastError();
        if (e == ERROR_ALREADY_EXISTS && (GetFileAttributesA(tmp) & FILE_ATTRIBUTE_DIRECTORY))
            return true;
        return false;
    }
    return true;
}

static uint8_t clamp8(int v)
{
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return (uint8_t)v;
}

bool write_ppm_yuy2(const char *path, const uint8_t *yuy2, uint32_t w, uint32_t h)
{
    FILE   *f;
    uint8_t *row;
    uint32_t y;

    if (!path || !yuy2 || (w & 1u) || w == 0 || h == 0)
        return false;

    row = (uint8_t *)malloc((size_t)w * 3u);
    if (!row)
        return false;

    f = fopen(path, "wb");
    if (!f) {
        free(row);
        return false;
    }

    fprintf(f, "P6\n%u %u\n255\n", w, h);

    for (y = 0; y < h; y++) {
        const uint8_t *src = yuy2 + (size_t)y * w * 2u;
        uint8_t       *dst = row;
        uint32_t       x;

        for (x = 0; x < w; x += 2u) {
            int y0 = src[0];
            int u  = src[1];
            int y1 = src[2];
            int v  = src[3];

            int c0 = y0 - 16;
            int c1 = y1 - 16;
            int d  = u - 128;
            int e  = v - 128;

            src += 4;

            dst[0] = clamp8((298 * c0 + 409 * e + 128) >> 8);
            dst[1] = clamp8((298 * c0 - 100 * d - 208 * e + 128) >> 8);
            dst[2] = clamp8((298 * c0 + 516 * d + 128) >> 8);

            dst[3] = clamp8((298 * c1 + 409 * e + 128) >> 8);
            dst[4] = clamp8((298 * c1 - 100 * d - 208 * e + 128) >> 8);
            dst[5] = clamp8((298 * c1 + 516 * d + 128) >> 8);

            dst += 6;
        }
        fwrite(row, 1, (size_t)w * 3u, f);
    }

    free(row);
    fclose(f);
    return true;
}

bool write_raw(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");

    if (!f)
        return false;
    if (len)
        fwrite(data, 1, len, f);
    fclose(f);
    return true;
}
