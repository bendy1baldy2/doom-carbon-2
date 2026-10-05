#include "doomgeneric.h"
#include "doomkeys.h"
#include "d_event.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <string.h>
#include <linux/fb.h>
#include <linux/input.h>

extern void D_PostEvent(event_t *ev);

unsigned char *screens[5] = {NULL, NULL, NULL, NULL, NULL};
unsigned char *I_VideoBuffer = NULL;

int vanilla_keyboard_mapping = 0;
int screensaver_mode = 0;
int screenvisible = 1;
int usegamma = 0;
int usemouse = 0;
int mouse_acceleration = 0;
int mouse_threshold = 0;

static int fb_fd = -1;
static int touch_fds[4] = {-1, -1, -1, -1};
static int num_touch_fds = 0;
static uint8_t *fb_mmap_ptr = NULL;
static uint32_t *backbuffer = NULL;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static uint32_t palette32[256];
static uint64_t start_time_ms = 0;

static int min_x = 0, max_x = 0;
static int min_y = 0, max_y = 0;

struct Btn { int x, y, w, h, key; const char *text; };
static struct Btn btns[18];
static int btns_inited = 0;

struct TouchSlot { int active, x, y; };
static struct TouchSlot slots[16];
static int current_slot = 0;
static int is_mt_device = 0;
static int protocol_a = 0;
static int btn_state[18] = {0};
static int target_state[18] = {0};

static const unsigned char font8x8[36][8] = {
    {0x3e,0x63,0x63,0x7f,0x63,0x63,0x63,0x00},{0x7e,0x63,0x63,0x7e,0x63,0x63,0x7e,0x00},
    {0x3e,0x63,0x60,0x60,0x60,0x63,0x3e,0x00},{0x7c,0x66,0x63,0x63,0x63,0x66,0x7c,0x00},
    {0x7f,0x60,0x60,0x7e,0x60,0x60,0x7f,0x00},{0x7f,0x60,0x60,0x7e,0x60,0x60,0x60,0x00},
    {0x3e,0x63,0x60,0x6f,0x63,0x63,0x3e,0x00},{0x63,0x63,0x63,0x7f,0x63,0x63,0x63,0x00},
    {0x3e,0x1c,0x1c,0x1c,0x1c,0x1c,0x3e,0x00},{0x0f,0x06,0x06,0x06,0x66,0x66,0x3c,0x00},
    {0x63,0x66,0x6c,0x78,0x6c,0x66,0x63,0x00},{0x60,0x60,0x60,0x60,0x60,0x60,0x7f,0x00},
    {0x63,0x77,0x7f,0x6b,0x63,0x63,0x63,0x00},{0x63,0x73,0x7b,0x6f,0x67,0x63,0x63,0x00},
    {0x3e,0x63,0x63,0x63,0x63,0x63,0x3e,0x00},{0x7e,0x63,0x63,0x7e,0x60,0x60,0x60,0x00},
    {0x3e,0x63,0x63,0x63,0x6b,0x66,0x3f,0x00},{0x7e,0x63,0x63,0x7e,0x6c,0x66,0x63,0x00},
    {0x3e,0x63,0x60,0x3e,0x03,0x63,0x3e,0x00},{0x7f,0x1c,0x1c,0x1c,0x1c,0x1c,0x1c,0x00},
    {0x63,0x63,0x63,0x63,0x63,0x63,0x3e,0x00},{0x63,0x63,0x63,0x63,0x63,0x36,0x1c,0x00},
    {0x63,0x63,0x63,0x6b,0x7f,0x77,0x63,0x00},{0x63,0x63,0x36,0x1c,0x36,0x63,0x63,0x00},
    {0x63,0x63,0x63,0x3e,0x1c,0x1c,0x1c,0x00},{0x7f,0x03,0x06,0x1c,0x30,0x60,0x7f,0x00},
    {0x3e,0x63,0x67,0x6b,0x73,0x63,0x3e,0x00},{0x18,0x38,0x18,0x18,0x18,0x18,0x3c,0x00},
    {0x3c,0x66,0x06,0x1c,0x30,0x66,0x7e,0x00},{0x3c,0x66,0x06,0x1c,0x06,0x66,0x3c,0x00},
    {0x0c,0x1c,0x3c,0x6c,0x7e,0x0c,0x0c,0x00},{0x7e,0x60,0x7c,0x06,0x06,0x66,0x3c,0x00},
    {0x1c,0x30,0x60,0x7c,0x66,0x66,0x3c,0x00},{0x7e,0x06,0x0c,0x18,0x30,0x30,0x30,0x00},
    {0x3c,0x66,0x66,0x3c,0x66,0x66,0x3c,0x00},{0x3c,0x66,0x66,0x3e,0x06,0x0c,0x38,0x00}
};

static uint64_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((uint64_t)tv.tv_sec * 1000ULL) + ((uint64_t)tv.tv_usec / 1000ULL);
}

static void init_buttons(int sw, int sh) {
    int bw = 48;
    int bh = 36;
    int gap = 4;
    int ox = 10;
    int oy = sh - (bh * 3 + gap * 2) - 10;

    btns[0] = (struct Btn){ox + bw + gap, oy, bw, bh, KEY_UPARROW, "FWD"};
    btns[1] = (struct Btn){ox + bw + gap, oy + (bh + gap) * 2, bw, bh, KEY_DOWNARROW, "BCK"};
    btns[2] = (struct Btn){ox, oy + bh + gap, bw, bh, KEY_STRAFE_L, "SL"};
    btns[3] = (struct Btn){ox + (bw + gap) * 2, oy + bh + gap, bw, bh, KEY_STRAFE_R, "SR"};
    btns[4] = (struct Btn){ox, oy, bw, bh, KEY_LEFTARROW, "TL"};
    btns[5] = (struct Btn){ox + (bw + gap) * 2, oy, bw, bh, KEY_RIGHTARROW, "TR"};

    int rx = sw - (bw * 2) - 10;
    int ry = sh - (bh * 2 + gap) - 10;
    btns[6] = (struct Btn){rx, ry + bh + gap, bw * 2, bh, KEY_FIRE, "FIRE"};
    btns[7] = (struct Btn){rx, ry, bw * 2, bh, KEY_USE, "USE"};

    btns[8] = (struct Btn){10, 10, 48, 30, KEY_ESCAPE, "ESC"};
    btns[9] = (struct Btn){sw - 58, 10, 48, 30, 13, "ENT"};

    int top_w = 32;
    int top_h = 30;
    int top_gap = 4;
    int total_top_w = 8 * top_w + 7 * top_gap;
    int start_top_x = (sw - total_top_w) / 2;

    btns[10] = (struct Btn){start_top_x, 10, top_w, top_h, 'Y', "Y"};

    const char *num_labels[6] = {"1", "2", "3", "4", "5", "6"};
    for (int i = 0; i < 6; i++) {
        btns[11 + i] = (struct Btn){start_top_x + (i + 1) * (top_w + top_gap), 10, top_w, top_h, '1' + i, num_labels[i]};
    }

    btns[17] = (struct Btn){start_top_x + 7 * (top_w + top_gap), 10, top_w, top_h, 'N', "N"};

    btns_inited = 1;
}

static void draw_text(int px, int py, const char *txt) {
    uint32_t roff = vinfo.red.length ? vinfo.red.offset : 16;
    uint32_t goff = vinfo.green.length ? vinfo.green.offset : 8;
    uint32_t boff = vinfo.blue.length ? vinfo.blue.offset : 0;
    uint32_t aoff = vinfo.transp.length ? vinfo.transp.offset : 24;
    uint32_t white = (0xFF << aoff) | (0xFF << roff) | (0xFF << goff) | (0xFF << boff);

    while (*txt) {
        const unsigned char *glyph = NULL;
        if (*txt >= 'A' && *txt <= 'Z') {
            glyph = font8x8[*txt - 'A'];
        } else if (*txt >= 'a' && *txt <= 'z') {
            glyph = font8x8[*txt - 'a'];
        } else if (*txt >= '0' && *txt <= '9') {
            glyph = font8x8[26 + (*txt - '0')];
        }
        if (glyph) {
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    if (glyph[y] & (1 << (7 - x))) {
                        int dx = px + x;
                        int dy = py + y;
                        if (dx >= 0 && dx < (int)vinfo.xres && dy >= 0 && dy < (int)vinfo.yres) {
                            backbuffer[dy * vinfo.xres + dx] = white;
                        }
                    }
                }
            }
        }
        px += 8;
        txt++;
    }
}

void DG_Init(void) {
    start_time_ms = get_time_ms();

    if (!I_VideoBuffer) {
        I_VideoBuffer = (unsigned char *)malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY);
    }
    screens[0] = I_VideoBuffer;

    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd >= 0) {
        ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);
        ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);
        if (!vinfo.xres) vinfo.xres = 800;
        if (!vinfo.yres) vinfo.yres = 480;
        if (!vinfo.bits_per_pixel) vinfo.bits_per_pixel = 32;
        vinfo.xoffset = 0;
        vinfo.yoffset = 0;
        ioctl(fb_fd, FBIOPAN_DISPLAY, &vinfo);
        fb_mmap_ptr = (uint8_t *)mmap(0, finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
        backbuffer = (uint32_t *)malloc(vinfo.xres * vinfo.yres * sizeof(uint32_t));
    }

    uint32_t roff = vinfo.red.length ? vinfo.red.offset : 16;
    uint32_t goff = vinfo.green.length ? vinfo.green.offset : 8;
    uint32_t boff = vinfo.blue.length ? vinfo.blue.offset : 0;
    uint32_t aoff = vinfo.transp.length ? vinfo.transp.offset : 24;
    for (int i = 0; i < 256; i++) {
        palette32[i] = (0xFF << aoff) | (i << roff) | (i << goff) | (i << boff);
    }

    const char *devs[] = {"/dev/input/event0", "/dev/input/event1", "/dev/input/event2", "/dev/input/event3"};
    for (int i = 0; i < 4; i++) {
        int fd = open(devs[i], O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            struct input_absinfo ai;
            int is_touch = 0;
            if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ai) >= 0 || ioctl(fd, EVIOCGABS(ABS_X), &ai) >= 0) {
                if (ai.maximum > max_x) {
                    min_x = ai.minimum;
                    max_x = ai.maximum;
                }
                is_touch = 1;
            }
            if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ai) >= 0 || ioctl(fd, EVIOCGABS(ABS_Y), &ai) >= 0) {
                if (ai.maximum > max_y) {
                    min_y = ai.minimum;
                    max_y = ai.maximum;
                }
                is_touch = 1;
            }
            if (is_touch) {
                touch_fds[num_touch_fds++] = fd;
            } else {
                close(fd);
            }
        }
    }
    if (max_x == 0) max_x = vinfo.xres ? vinfo.xres : 800;
    if (max_y == 0) max_y = vinfo.yres ? vinfo.yres : 480;

    init_buttons(vinfo.xres ? vinfo.xres : 800, vinfo.yres ? vinfo.yres : 480);
}

void I_SetPalette(unsigned char *palette) {
    uint32_t roff = vinfo.red.length ? vinfo.red.offset : 16;
    uint32_t goff = vinfo.green.length ? vinfo.green.offset : 8;
    uint32_t boff = vinfo.blue.length ? vinfo.blue.offset : 0;
    uint32_t aoff = vinfo.transp.length ? vinfo.transp.offset : 24;

    for (int i = 0; i < 256; i++) {
        uint32_t r = palette[i * 3];
        uint32_t g = palette[i * 3 + 1];
        uint32_t b = palette[i * 3 + 2];
        if (vinfo.bits_per_pixel == 16) {
            palette32[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        } else {
            palette32[i] = (0xFF << aoff) | (r << roff) | (g << goff) | (b << boff);
        }
    }
}

void DG_DrawFrame(void) {
    if (!fb_mmap_ptr || !backbuffer) return;

    uint32_t sw = vinfo.xres;
    uint32_t sh = vinfo.yres;

    if (!btns_inited) {
        init_buttons(sw, sh);
    }

    unsigned char *src_pixels = screens[0] ? screens[0] : I_VideoBuffer;
    if (src_pixels) {
        for (uint32_t y = 0; y < sh; y++) {
            int src_y = (y * DOOMGENERIC_RESY) / sh;
            unsigned char *src_row = &src_pixels[src_y * DOOMGENERIC_RESX];
            uint32_t *dst_row = &backbuffer[y * sw];
            for (uint32_t x = 0; x < sw; x++) {
                dst_row[x] = palette32[src_row[(x * DOOMGENERIC_RESX) / sw]];
            }
        }
    }

    uint32_t roff = vinfo.red.length ? vinfo.red.offset : 16;
    uint32_t goff = vinfo.green.length ? vinfo.green.offset : 8;
    uint32_t boff = vinfo.blue.length ? vinfo.blue.offset : 0;
    uint32_t aoff = vinfo.transp.length ? vinfo.transp.offset : 24;
    uint32_t white = (0xFF << aoff) | (0xFF << roff) | (0xFF << goff) | (0xFF << boff);

    for (int i = 0; i < 18; i++) {
        struct Btn b = btns[i];
        for (int y = b.y; y < b.y + b.h; y++) {
            for (int x = b.x; x < b.x + b.w; x++) {
                if (x == b.x || x == b.x + b.w - 1 || y == b.y || y == b.y + b.h - 1) {
                    backbuffer[y * sw + x] = white;
                } else {
                    uint32_t bg = backbuffer[y * sw + x];
                    if (vinfo.bits_per_pixel == 16) {
                        uint32_t r = (bg >> 11) & 0x1F;
                        uint32_t g = (bg >> 5) & 0x3F;
                        uint32_t bl = bg & 0x1F;
                        r = (r * 3 + 31) / 4;
                        g = (g * 3 + 63) / 4;
                        bl = (bl * 3 + 31) / 4;
                        backbuffer[y * sw + x] = (r << 11) | (g << 5) | bl;
                    } else {
                        uint32_t r = (bg >> roff) & 0xFF;
                        uint32_t g = (bg >> goff) & 0xFF;
                        uint32_t bl = (bg >> boff) & 0xFF;
                        r = (r * 3 + 255) / 4;
                        g = (g * 3 + 255) / 4;
                        bl = (bl * 3 + 255) / 4;
                        backbuffer[y * sw + x] = (0xFF << aoff) | (r << roff) | (g << goff) | (bl << boff);
                    }
                }
            }
        }
        int tw = strlen(b.text) * 8;
        draw_text(b.x + (b.w - tw) / 2, b.y + (b.h - 8) / 2, b.text);
    }

    uint32_t pitch = finfo.line_length ? finfo.line_length : (sw * 4);
    for (uint32_t y = 0; y < sh; y++) {
        uint8_t *dst_line = fb_mmap_ptr + (y * pitch);
        if (vinfo.bits_per_pixel == 16) {
            uint16_t *d16 = (uint16_t *)dst_line;
            uint32_t *s32 = &backbuffer[y * sw];
            for (uint32_t x = 0; x < sw; x++) d16[x] = (uint16_t)s32[x];
        } else {
            memcpy(dst_line, &backbuffer[y * sw], sw * 4);
        }
    }
}

void I_FinishUpdate(void) {
    DG_DrawFrame();
}

void I_ReadScreen(unsigned char *scr) {
    if (screens[0]) memcpy(scr, screens[0], DOOMGENERIC_RESX * DOOMGENERIC_RESY);
}

void I_UpdateNoBlit(void) {}
void I_StartFrame(void) {}
void DG_SleepMs(uint32_t ms) { usleep(ms * 1000); }
uint32_t DG_GetTicksMs(void) { return (uint32_t)(get_time_ms() - start_time_ms); }

int DG_GetKey(int *pressed, unsigned char *doomKey) {
    struct input_event ev;
    uint32_t sw = vinfo.xres ? vinfo.xres : 800;
    uint32_t sh = vinfo.yres ? vinfo.yres : 480;

    for (int i = 0; i < num_touch_fds; i++) {
        while (read(touch_fds[i], &ev, sizeof(ev)) > 0) {
            if (ev.type == EV_ABS) {
                if (ev.code == ABS_MT_SLOT) {
                    if (ev.value >= 0 && ev.value < 16) {
                        current_slot = ev.value;
                        is_mt_device = 1;
                    }
                } else if (ev.code == ABS_MT_TRACKING_ID) {
                    is_mt_device = 1;
                    if (ev.value == -1) {
                        slots[current_slot].active = 0;
                    } else {
                        slots[current_slot].active = 1;
                    }
                } else if (ev.code == ABS_MT_POSITION_X) {
                    is_mt_device = 1;
                    slots[current_slot].x = (max_x > min_x) ? ((ev.value - min_x) * (int)sw) / (max_x - min_x) : ev.value;
                } else if (ev.code == ABS_MT_POSITION_Y) {
                    is_mt_device = 1;
                    slots[current_slot].y = (max_y > min_y) ? ((ev.value - min_y) * (int)sh) / (max_y - min_y) : ev.value;
                } else if (ev.code == ABS_X) {
                    if (!is_mt_device) {
                        slots[0].x = (max_x > min_x) ? ((ev.value - min_x) * (int)sw) / (max_x - min_x) : ev.value;
                    }
                } else if (ev.code == ABS_Y) {
                    if (!is_mt_device) {
                        slots[0].y = (max_y > min_y) ? ((ev.value - min_y) * (int)sh) / (max_y - min_y) : ev.value;
                    }
                }
            } else if (ev.type == EV_KEY) {
                if (ev.code == BTN_TOUCH || ev.code == BTN_LEFT) {
                    if (!is_mt_device) {
                        slots[0].active = ev.value;
                    }
                }
            } else if (ev.type == EV_SYN) {
                if (ev.code == SYN_MT_REPORT) {
                    protocol_a = 1;
                    is_mt_device = 1;
                    if (current_slot < 15) current_slot++;
                } else if (ev.code == SYN_REPORT) {
                    if (protocol_a) {
                        for (int s = current_slot; s < 16; s++) slots[s].active = 0;
                        current_slot = 0;
                        protocol_a = 0;
                    }
                }
            }
        }
    }

    memset(target_state, 0, sizeof(target_state));

    for (int s = 0; s < 16; s++) {
        if (!slots[s].active) continue;
        int sx = slots[s].x;
        int sy = slots[s].y;
        for (int i = 0; i < 18; i++) {
            if (sx >= btns[i].x && sx <= (btns[i].x + btns[i].w) &&
                sy >= btns[i].y && sy <= (btns[i].y + btns[i].h)) {
                target_state[i] = 1;
            }
        }
    }

    for (int i = 0; i < 18; i++) {
        if (target_state[i] != btn_state[i]) {
            btn_state[i] = target_state[i];
            *pressed = btn_state[i];
            *doomKey = (unsigned char)btns[i].key;
            return 1;
        }
    }

    return 0;
}

void I_StartTic(void) {
    int pressed;
    unsigned char doomKey;
    while (DG_GetKey(&pressed, &doomKey)) {
        event_t ev;
        ev.type = pressed ? ev_keydown : ev_keyup;
        ev.data1 = doomKey;
        ev.data2 = 0;
        ev.data3 = 0;
        D_PostEvent(&ev);
        if (pressed && doomKey >= 'a' && doomKey <= 'z') {
            ev.data1 = doomKey - 32;
            D_PostEvent(&ev);
        } else if (!pressed && doomKey >= 'a' && doomKey <= 'z') {
            ev.data1 = doomKey - 32;
            D_PostEvent(&ev);
        } else if (pressed && doomKey >= 'A' && doomKey <= 'Z') {
            ev.data1 = doomKey + 32;
            D_PostEvent(&ev);
        } else if (!pressed && doomKey >= 'A' && doomKey <= 'Z') {
            ev.data1 = doomKey + 32;
            D_PostEvent(&ev);
        }
    }
}

void DG_SetWindowTitle(const char *title) {}
void I_InitInput(void) {}
void I_GetEvent(void) {}
void I_BindVideoVariables(void) {}
void I_SetWindowTitle(char *title) {}
void I_GraphicsCheckCommandLine(void) {}
void I_SetGrabMouseCallback(void *func) {}
void I_InitGraphics(void) {
    if (!I_VideoBuffer) {
        I_VideoBuffer = (unsigned char *)malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY);
    }
    screens[0] = I_VideoBuffer;
}
void I_EnableLoadingDisk(void) {}
void I_DisplayFPSDots(int boolean) {}
int I_CheckIsScreensaver(void) { return 0; }
int I_GetPaletteIndex(int r, int g, int b) { return 0; }
void I_BeginRead(void) {}
void I_EndRead(void) {}

int main(int argc, char **argv) {
    doomgeneric_Create(argc, argv);
    while (1) doomgeneric_Tick();
    return 0;
}