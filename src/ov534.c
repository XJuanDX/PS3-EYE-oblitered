/* ov534.c — OV534 bridge + OV7720 sensor bring-up over WinUSB.
 *
 * The register tables and the SCCB/register-sequencing logic below are ported
 * from the Linux kernel driver:
 *
 *     drivers/media/usb/gspca/ov534.c  (v6.6)
 *     Copyright (C) 2008 Antonio Ospite
 *     Copyright (C) 2008 Jim Paris
 *     Copyright (C) 2009 Jean-Francois Moine
 *     PS3 Eye enhancements by Richard Kaswy, Max Thrun, Joseph Howse
 *     SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Only the OV7720/OV7725 path is implemented. The OV7670 variant streams
 * JPEG and is rejected explicitly instead of producing garbage.
 */
#include <math.h>
#include <string.h>

#include "ov534.h"

#define PS3EYE_PI 3.14159265358979323846

/* Bridge registers used to reach the sensor over SCCB. */
#define OV534_REG_ADDRESS  0xf1
#define OV534_REG_SUBADDR  0xf2
#define OV534_REG_WRITE    0xf3
#define OV534_REG_READ     0xf4
#define OV534_REG_OPERATION 0xf5
#define OV534_REG_STATUS   0xf6

#define OV534_OP_WRITE_3   0x37
#define OV534_OP_WRITE_2   0x33
#define OV534_OP_READ_2    0xf9

typedef struct {
    uint8_t reg;
    uint8_t val;
} reg_pair;

/* ------------------------------------------------------------------ */
/* register tables — verbatim from gspca_ov534.c                       */
/* ------------------------------------------------------------------ */

static const reg_pair bridge_init_772x[] = {
    { 0x88, 0xf8 }, { 0x89, 0xff }, { 0x76, 0x03 }, { 0x92, 0x01 },
    { 0x93, 0x18 }, { 0x94, 0x10 }, { 0x95, 0x10 }, { 0xe2, 0x00 },
    { 0xe7, 0x3e }, { 0x96, 0x00 },
    { 0x97, 0x20 }, { 0x97, 0x20 }, { 0x97, 0x20 }, { 0x97, 0x0a },
    { 0x97, 0x3f }, { 0x97, 0x4a }, { 0x97, 0x20 }, { 0x97, 0x15 },
    { 0x97, 0x0b },
    { 0x8e, 0x40 }, { 0x1f, 0x81 }, { 0x34, 0x05 }, { 0xe3, 0x04 },
    { 0x89, 0x00 }, { 0x76, 0x00 }, { 0xe7, 0x2e }, { 0x31, 0xf9 },
    { 0x25, 0x42 }, { 0x21, 0xf0 },
    { 0x1c, 0x0a }, { 0x1d, 0x08 }, { 0x1d, 0x0e },
};

static const reg_pair sensor_init_772x[] = {
    { 0x12, 0x80 },
    { 0x11, 0x01 }, { 0x11, 0x01 }, { 0x11, 0x01 }, { 0x11, 0x01 },
    { 0x11, 0x01 }, { 0x11, 0x01 }, { 0x11, 0x01 }, { 0x11, 0x01 },
    { 0x11, 0x01 }, { 0x11, 0x01 }, { 0x11, 0x01 },
    { 0x3d, 0x03 }, { 0x17, 0x26 }, { 0x18, 0xa0 }, { 0x19, 0x07 },
    { 0x1a, 0xf0 }, { 0x32, 0x00 }, { 0x29, 0xa0 }, { 0x2c, 0xf0 },
    { 0x65, 0x20 }, { 0x11, 0x01 }, { 0x42, 0x7f }, { 0x63, 0xaa },
    { 0x64, 0xff }, { 0x66, 0x00 }, { 0x13, 0xf0 }, { 0x0d, 0x41 },
    { 0x0f, 0xc5 }, { 0x14, 0x11 },
    { 0x22, 0x7f }, { 0x23, 0x03 }, { 0x24, 0x40 }, { 0x25, 0x30 },
    { 0x26, 0xa1 }, { 0x2a, 0x00 }, { 0x2b, 0x00 }, { 0x6b, 0xaa },
    { 0x13, 0xff },
    { 0x90, 0x05 }, { 0x91, 0x01 }, { 0x92, 0x03 }, { 0x93, 0x00 },
    { 0x94, 0x60 }, { 0x95, 0x3c }, { 0x96, 0x24 }, { 0x97, 0x1e },
    { 0x98, 0x62 }, { 0x99, 0x80 }, { 0x9a, 0x1e }, { 0x9b, 0x08 },
    { 0x9c, 0x20 }, { 0x9e, 0x81 },
    { 0xa6, 0x07 },
    { 0x7e, 0x0c }, { 0x7f, 0x16 }, { 0x80, 0x2a }, { 0x81, 0x4e },
    { 0x82, 0x61 }, { 0x83, 0x6f }, { 0x84, 0x7b }, { 0x85, 0x86 },
    { 0x86, 0x8e }, { 0x87, 0x97 }, { 0x88, 0xa4 }, { 0x89, 0xaf },
    { 0x8a, 0xc5 }, { 0x8b, 0xd7 }, { 0x8c, 0xe8 }, { 0x8d, 0x20 },
    { 0x2b, 0x00 }, { 0x22, 0x7f }, { 0x23, 0x03 }, { 0x11, 0x01 },
    { 0x64, 0xff }, { 0x0d, 0x41 },
    { 0x14, 0x41 }, { 0x0e, 0xcd }, { 0xac, 0xbf }, { 0x8e, 0x00 },
};

static const reg_pair bridge_start_vga_yuyv_772x[] = {
    { 0x88, 0x00 }, { 0x1c, 0x00 }, { 0x1d, 0x40 }, { 0x1d, 0x02 },
    { 0x1d, 0x00 }, { 0x1d, 0x02 }, { 0x1d, 0x58 }, { 0x1d, 0x00 },
    { 0x8d, 0x1c }, { 0x8e, 0x80 }, { 0xc0, 0x50 }, { 0xc1, 0x3c },
    { 0xc2, 0x0c }, { 0xc3, 0x69 },
};

static const reg_pair sensor_start_vga_yuyv_772x[] = {
    { 0x12, 0x00 }, { 0x17, 0x26 }, { 0x18, 0xa0 }, { 0x19, 0x07 },
    { 0x1a, 0xf0 }, { 0x29, 0xa0 }, { 0x2c, 0xf0 }, { 0x65, 0x20 },
    { 0x67, 0x00 },
};

static const reg_pair bridge_start_qvga_yuyv_772x[] = {
    { 0x88, 0x00 }, { 0x1c, 0x00 }, { 0x1d, 0x40 }, { 0x1d, 0x02 },
    { 0x1d, 0x00 }, { 0x1d, 0x01 }, { 0x1d, 0x4b }, { 0x1d, 0x00 },
    { 0x8d, 0x1c }, { 0x8e, 0x80 }, { 0xc0, 0x28 }, { 0xc1, 0x1e },
    { 0xc2, 0x0c }, { 0xc3, 0x69 },
};

static const reg_pair sensor_start_qvga_yuyv_772x[] = {
    { 0x12, 0x40 }, { 0x17, 0x3f }, { 0x18, 0x50 }, { 0x19, 0x03 },
    { 0x1a, 0x78 }, { 0x29, 0x50 }, { 0x2c, 0x78 }, { 0x65, 0x2f },
    { 0x67, 0x00 },
};

/* ------------------------------------------------------------------ */
/* geometry                                                            */
/* ------------------------------------------------------------------ */

uint32_t ov534_width(ps3eye_mode m)
{
    return m == PS3EYE_MODE_QVGA_320x240 ? 320u : 640u;
}

uint32_t ov534_height(ps3eye_mode m)
{
    return m == PS3EYE_MODE_QVGA_320x240 ? 240u : 480u;
}

uint32_t ov534_frame_bytes(ps3eye_mode m)
{
    return ov534_width(m) * ov534_height(m) * 2u;
}

const char *ov534_mode_name(ps3eye_mode m)
{
    return m == PS3EYE_MODE_QVGA_320x240 ? "320x240" : "640x480";
}

/* ------------------------------------------------------------------ */
/* low-level bridge + SCCB access                                      */
/* ------------------------------------------------------------------ */

bool ov534_bridge_write(ov534 *c, uint8_t reg, uint8_t val)
{
    /* gspca: bRequest 0x01, bmRequestType 0x40, wValue 0, wIndex = reg. */
    if (!ps3eye_ctrl_out(c->dev, 0x01, 0x0000, reg, &val, 1)) {
        c->usb_errors++;
        return false;
    }
    return true;
}

bool ov534_bridge_read(ov534 *c, uint8_t reg, uint8_t *val)
{
    uint32_t got = 0;

    *val = 0;
    /* gspca: bRequest 0x01, bmRequestType 0xc0, wValue 0, wIndex = reg. */
    if (!ps3eye_ctrl_in(c->dev, 0x01, 0x0000, reg, val, 1, &got) || got != 1) {
        c->usb_errors++;
        *val = 0;
        return false;
    }
    return true;
}

static int sccb_check_status(ov534 *c)
{
    int i;

    for (i = 0; i < 5; i++) {
        uint8_t data = 0;

        Sleep(10);
        if (!ov534_bridge_read(c, OV534_REG_STATUS, &data))
            return 0;

        switch (data) {
        case 0x00:
            return 1;
        case 0x04:
            return 0;
        case 0x03:
            break;
        default:
            break;
        }
    }
    return 0;
}

bool ov534_sccb_write(ov534 *c, uint8_t reg, uint8_t val)
{
    int ok = 1;

    ok &= (int)ov534_bridge_write(c, OV534_REG_SUBADDR, reg);
    ok &= (int)ov534_bridge_write(c, OV534_REG_WRITE, val);
    ok &= (int)ov534_bridge_write(c, OV534_REG_OPERATION, OV534_OP_WRITE_3);

    if (!sccb_check_status(c)) {
        c->usb_errors++;
        ok = 0;
    }
    return ok != 0;
}

bool ov534_sccb_read(ov534 *c, uint8_t reg, uint8_t *val)
{
    uint8_t dummy = 0;

    *val = 0;

    ov534_bridge_write(c, OV534_REG_SUBADDR, reg);
    ov534_bridge_write(c, OV534_REG_OPERATION, OV534_OP_WRITE_2);
    if (!sccb_check_status(c)) {
        c->usb_errors++;
        /* gspca keeps going; the read below is what actually fails. */
    }

    ov534_bridge_write(c, OV534_REG_OPERATION, OV534_OP_READ_2);
    if (!sccb_check_status(c))
        c->usb_errors++;

    dummy = 0;
    (void)dummy;

    return ov534_bridge_read(c, OV534_REG_READ, val);
}

static void reg_w_array(ov534 *c, const reg_pair *data, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++)
        ov534_bridge_write(c, data[i].reg, data[i].val);
}

static void sccb_w_array(ov534 *c, const reg_pair *data, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (data[i].reg != 0xff) {
            ov534_sccb_write(c, data[i].reg, data[i].val);
        } else {
            uint8_t v = 0;
            ov534_sccb_read(c, data[i].val, &v);
            ov534_sccb_write(c, 0xff, 0x00);
        }
    }
}

/* Two bits control the LED: 0x21 bit 7 and 0x23 bit 7. */
static void ov534_set_led(ov534 *c, int on)
{
    uint8_t v = 0;

    ov534_bridge_read(c, 0x21, &v);
    v |= 0x80;
    ov534_bridge_write(c, 0x21, v);

    ov534_bridge_read(c, 0x23, &v);
    if (on)
        v |= 0x80;
    else
        v &= (uint8_t)~0x80;
    ov534_bridge_write(c, 0x23, v);

    if (!on) {
        ov534_bridge_read(c, 0x21, &v);
        v &= (uint8_t)~0x80;
        ov534_bridge_write(c, 0x21, v);
    }
}

/* ------------------------------------------------------------------ */
/* frame rate                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t fps;
    uint8_t r11;
    uint8_t r0d;
    uint8_t re5;
} rate_entry;

static const rate_entry rate_vga[] = {
    { 60, 0x01, 0xc1, 0x04 },
    { 50, 0x01, 0x41, 0x02 },
    { 40, 0x02, 0xc1, 0x04 },
    { 30, 0x04, 0x81, 0x02 },
    { 15, 0x03, 0x41, 0x04 },
};

static const rate_entry rate_qvga[] = {
    { 187, 0x01, 0x81, 0x02 },
    { 150, 0x01, 0xc1, 0x04 },
    { 137, 0x02, 0xc1, 0x02 },
    { 125, 0x02, 0x81, 0x02 },
    { 100, 0x02, 0xc1, 0x04 },
    { 75,  0x03, 0xc1, 0x04 },
    { 60,  0x04, 0xc1, 0x04 },
    { 50,  0x02, 0x41, 0x04 },
    { 37,  0x03, 0x41, 0x04 },
    { 30,  0x04, 0x41, 0x04 },
};

static void set_frame_rate(ov534 *c, uint8_t fps, ps3eye_mode mode)
{
    const rate_entry *r;
    int               n, i;

    if (mode == PS3EYE_MODE_VGA_640x480) {
        r = rate_vga;
        n = (int)(sizeof(rate_vga) / sizeof(rate_vga[0]));
    } else {
        r = rate_qvga;
        n = (int)(sizeof(rate_qvga) / sizeof(rate_qvga[0]));
    }

    i = n;
    while (--i > 0) {
        if (fps >= r->fps)
            break;
        r++;
    }

    ov534_sccb_write(c, 0x11, r->r11);
    ov534_sccb_write(c, 0x0d, r->r0d);
    ov534_bridge_write(c, 0xe5, r->re5);

    c->fps = r->fps;
}

/* ------------------------------------------------------------------ */
/* image controls (defaults only; full mapping is stage 4)             */
/* ------------------------------------------------------------------ */

static int fixp_sin16(uint16_t x)
{
    return (int)lround(sin((double)x * (2.0 * PS3EYE_PI / 65536.0)) * 32767.0);
}

static int fixp_cos16(uint16_t x)
{
    return (int)lround(cos((double)x * (2.0 * PS3EYE_PI / 65536.0)) * 32767.0);
}

void ov534_set_hue(ov534 *c, int val)
{
    int     huesin = fixp_sin16((uint16_t)val) * 0x80 / 0x7fff;
    int     huecos = fixp_cos16((uint16_t)val) * 0x80 / 0x7fff;
    uint8_t ab = 0;

    if (huesin < 0) {
        ov534_sccb_read(c, 0xab, &ab);
        ov534_sccb_write(c, 0xab, (uint8_t)(ab | 0x02));
        huesin = -huesin;
    } else {
        ov534_sccb_read(c, 0xab, &ab);
        ov534_sccb_write(c, 0xab, (uint8_t)(ab & ~0x02));
    }

    ov534_sccb_write(c, 0xa9, (uint8_t)huecos);
    ov534_sccb_write(c, 0xaa, (uint8_t)huesin);
}

void ov534_set_saturation(ov534 *c, int val)
{
    ov534_sccb_write(c, 0xa7, (uint8_t)val);
    ov534_sccb_write(c, 0xa8, (uint8_t)val);
}

void ov534_set_brightness(ov534 *c, int val)
{
    ov534_sccb_write(c, 0x9b, (uint8_t)val);
}

void ov534_set_contrast(ov534 *c, int val)
{
    ov534_sccb_write(c, 0x9c, (uint8_t)val);
}

void ov534_set_gain(ov534 *c, int val)
{
    switch (val & 0x30) {
    case 0x00:
        val &= 0x0f;
        break;
    case 0x10:
        val &= 0x0f;
        val |= 0x30;
        break;
    case 0x20:
        val &= 0x0f;
        val |= 0x70;
        break;
    default:
        val &= 0x0f;
        val |= 0xf0;
        break;
    }
    ov534_sccb_write(c, 0x00, (uint8_t)val);
}

void ov534_set_exposure(ov534 *c, int val)
{
    ov534_sccb_write(c, 0x08, (uint8_t)(val >> 7));
    ov534_sccb_write(c, 0x10, (uint8_t)(val << 1));
}

void ov534_set_agc(ov534 *c, int on)
{
    uint8_t v = 0;

    if (on) {
        ov534_sccb_read(c, 0x13, &v);
        ov534_sccb_write(c, 0x13, (uint8_t)(v | 0x04));
        ov534_sccb_read(c, 0x64, &v);
        ov534_sccb_write(c, 0x64, (uint8_t)(v | 0x03));
    } else {
        ov534_sccb_read(c, 0x13, &v);
        ov534_sccb_write(c, 0x13, (uint8_t)(v & ~0x04));
        ov534_sccb_read(c, 0x64, &v);
        ov534_sccb_write(c, 0x64, (uint8_t)(v & ~0x03));
    }
}

void ov534_set_awb(ov534 *c, int on)
{
    uint8_t v = 0;

    if (on) {
        ov534_sccb_read(c, 0x13, &v);
        ov534_sccb_write(c, 0x13, (uint8_t)(v | 0x02));
        ov534_sccb_read(c, 0x63, &v);
        ov534_sccb_write(c, 0x63, (uint8_t)(v | 0xc0));
    } else {
        ov534_sccb_read(c, 0x13, &v);
        ov534_sccb_write(c, 0x13, (uint8_t)(v & ~0x02));
        ov534_sccb_read(c, 0x63, &v);
        ov534_sccb_write(c, 0x63, (uint8_t)(v & ~0xc0));
    }
}

void ov534_set_aec(ov534 *c, int auto_exposure)
{
    uint8_t v = 0;

    ov534_sccb_read(c, 0x13, &v);
    if (auto_exposure)
        ov534_sccb_write(c, 0x13, (uint8_t)(v | 0x01));
    else
        ov534_sccb_write(c, 0x13, (uint8_t)(v & ~0x01));
}

void ov534_set_sharpness(ov534 *c, int val)
{
    ov534_sccb_write(c, 0x91, (uint8_t)val);
    ov534_sccb_write(c, 0x8e, (uint8_t)val);
}

void ov534_set_hvflip(ov534 *c, int hflip, int vflip)
{
    uint8_t v = 0;

    ov534_sccb_read(c, 0x0c, &v);
    v &= (uint8_t)~0xc0;
    if (hflip == 0)
        v |= 0x40;
    if (vflip == 0)
        v |= 0x80;
    ov534_sccb_write(c, 0x0c, v);
}

void ov534_set_light_freq(ov534 *c, int fifty_hz)
{
    ov534_sccb_write(c, 0x2b, (uint8_t)(fifty_hz ? 0x9e : 0x00));
}

/* gspca applies these in sd_start() using the v4l2 control defaults. */
static void apply_default_controls(ov534 *c)
{
    ov534_set_hue(c, 0);            /* V4L2_CID_HUE default */
    ov534_set_saturation(c, 64);    /* V4L2_CID_SATURATION default */
    ov534_set_agc(c, 1);            /* V4L2_CID_AUTOGAIN default */
    ov534_set_awb(c, 1);            /* V4L2_CID_AUTO_WHITE_BALANCE default */
    ov534_set_aec(c, 1);            /* V4L2_EXPOSURE_AUTO */
    ov534_set_gain(c, 20);          /* V4L2_CID_GAIN default */
    ov534_set_exposure(c, 120);     /* V4L2_CID_EXPOSURE default */
    ov534_set_brightness(c, 0);     /* V4L2_CID_BRIGHTNESS default */
    ov534_set_contrast(c, 32);      /* V4L2_CID_CONTRAST default */
    ov534_set_sharpness(c, 0);      /* V4L2_CID_SHARPNESS default */
    ov534_set_hvflip(c, 0, 0);      /* V4L2_CID_HFLIP/VFLIP default */
    ov534_set_light_freq(c, 0);     /* V4L2_CID_POWER_LINE_FREQUENCY default */
}

/* ------------------------------------------------------------------ */
/* public entry points                                                 */
/* ------------------------------------------------------------------ */

bool ov534_init(ov534 *c, ps3eye_dev *dev, char *err, size_t errlen)
{
    uint8_t hi = 0, lo = 0;

    memset(c, 0, sizeof(*c));
    c->dev = dev;

    if (errlen)
        err[0] = '\0';

    /* Reset the bridge. */
    ov534_bridge_write(c, 0xe7, 0x3a);
    ov534_bridge_write(c, 0xe0, 0x08);
    Sleep(100);

    /* Sensor address on the SCCB bus. */
    ov534_bridge_write(c, OV534_REG_ADDRESS, 0x42);

    /* Reset the sensor. */
    ov534_sccb_write(c, 0x12, 0x80);
    Sleep(12);

    /* Probe: the first read of each register is discarded, as in gspca. */
    ov534_sccb_read(c, 0x0a, &hi);
    ov534_sccb_read(c, 0x0a, &hi);
    ov534_sccb_read(c, 0x0b, &lo);
    ov534_sccb_read(c, 0x0b, &lo);
    c->sensor_id = (uint16_t)(((uint16_t)hi << 8) | lo);

    if (c->sensor_id == 0x0000 || c->sensor_id == 0xffff) {
        _snprintf_s(err, errlen, _TRUNCATE,
                    "sensor ID read as 0x%04x: SCCB not responding "
                    "(usb_errors=%u). Check that official PS3 Eye software "
                    "is not holding the device.",
                    c->sensor_id, c->usb_errors);
        return false;
    }

    if ((c->sensor_id & 0xfff0) == 0x7670) {
        _snprintf_s(err, errlen, _TRUNCATE,
                    "sensor ID 0x%04x is an OV7670 variant: it streams JPEG and "
                    "is not supported by this build", c->sensor_id);
        return false;
    }

    reg_w_array(c, bridge_init_772x,
                sizeof(bridge_init_772x) / sizeof(bridge_init_772x[0]));
    ov534_set_led(c, 1);
    sccb_w_array(c, sensor_init_772x,
                 sizeof(sensor_init_772x) / sizeof(sensor_init_772x[0]));

    ov534_stop(c);

    if (c->usb_errors > 0) {
        _snprintf_s(err, errlen, _TRUNCATE,
                    "initialised with %u USB/SCCB errors", c->usb_errors);
        return false;
    }
    return true;
}

bool ov534_start(ov534 *c, ps3eye_mode mode, uint8_t fps, char *err, size_t errlen)
{
    unsigned before = c->usb_errors;

    if (errlen)
        err[0] = '\0';

    if (mode == PS3EYE_MODE_VGA_640x480) {
        reg_w_array(c, bridge_start_vga_yuyv_772x,
                    sizeof(bridge_start_vga_yuyv_772x) / sizeof(bridge_start_vga_yuyv_772x[0]));
        sccb_w_array(c, sensor_start_vga_yuyv_772x,
                     sizeof(sensor_start_vga_yuyv_772x) / sizeof(sensor_start_vga_yuyv_772x[0]));
    } else {
        reg_w_array(c, bridge_start_qvga_yuyv_772x,
                    sizeof(bridge_start_qvga_yuyv_772x) / sizeof(bridge_start_qvga_yuyv_772x[0]));
        sccb_w_array(c, sensor_start_qvga_yuyv_772x,
                     sizeof(sensor_start_qvga_yuyv_772x) / sizeof(sensor_start_qvga_yuyv_772x[0]));
    }

    set_frame_rate(c, fps, mode);
    apply_default_controls(c);

    ov534_set_led(c, 1);
    ov534_bridge_write(c, 0xe0, 0x00); /* start streaming */

    c->mode      = mode;
    c->streaming = true;

    if (c->usb_errors > before) {
        _snprintf_s(err, errlen, _TRUNCATE,
                    "start reported %u USB/SCCB errors",
                    c->usb_errors - before);
        return false;
    }
    return true;
}

bool ov534_stop(ov534 *c)
{
    if (!c || !c->dev)
        return false;

    ov534_bridge_write(c, 0xe0, 0x09); /* stop streaming */
    ov534_set_led(c, 0);
    c->streaming = false;
    return true;
}
