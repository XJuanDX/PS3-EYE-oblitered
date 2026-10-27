/* image_out.h — minimal on-disk output for captured frames. */
#ifndef IMAGE_OUT_H
#define IMAGE_OUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Create `path` and any missing parents. Returns true if it exists after. */
bool ensure_dir(const char *path);

/* YUY2 (packed 4:2:2) -> binary PPM (P6, 8 bit per channel). */
bool write_ppm_yuy2(const char *path, const uint8_t *yuy2, uint32_t w, uint32_t h);

bool write_raw(const char *path, const void *data, size_t len);

#endif /* IMAGE_OUT_H */
