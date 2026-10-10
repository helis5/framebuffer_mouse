#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

static unsigned int g_line_length = 0;
static volatile int g_running = 1;

static void on_sigint(int sig) { (void)sig; g_running = 0; }

static inline unsigned int make_pixel(struct fb_var_screeninfo *vi,
                                       unsigned char r, unsigned char g, unsigned char b) {
    return (r << vi->red.offset)   |
           (g << vi->green.offset) |
           (b << vi->blue.offset)  |
           (0xff << vi->transp.offset);
}

static inline void put_pixel(char *fb, struct fb_var_screeninfo *vi,
                             int x, int y, unsigned int color) {
    if (x < 0 || y < 0 || x >= (int)vi->xres || y >= (int)vi->yres) return;
    long loc = (x + vi->xoffset) * (vi->bits_per_pixel / 8)
             + (y + vi->yoffset) * g_line_length;
    *((unsigned int *)(fb + loc)) = color;
}

static inline unsigned int get_pixel(char *fb, struct fb_var_screeninfo *vi,
                                     int x, int y) {
    if (x < 0 || y < 0 || x >= (int)vi->xres || y >= (int)vi->yres) return 0;
    long loc = (x + vi->xoffset) * (vi->bits_per_pixel / 8)
             + (y + vi->yoffset) * g_line_length;
    return *((unsigned int *)(fb + loc));
}

// Курсор: 8x12. 'X' = чёрный, '.' = белый, ' ' = прозрачно
#define CUR_W 8
#define CUR_H 12
static const char *cursor_bitmap[CUR_H] = {
    "X       ",
    "XX      ",
    "X.X     ",
    "X..X    ",
    "X...X   ",
    "X....X  ",
    "X.....X ",
    "X......X",
    "X...XXXX",
    "X..X    ",
    "X.X     ",
    "XX      ",
};

static unsigned int saved[CUR_W * CUR_H];
static int old_cx = -1, old_cy = -1;

static void erase_cursor(char *fb, struct fb_var_screeninfo *vi) {
    if (old_cx < 0) return;
    for (int j = 0; j < CUR_H; j++)
        for (int i = 0; i < CUR_W; i++)
            put_pixel(fb, vi, old_cx + i, old_cy + j, saved[j * CUR_W + i]);
    old_cx = -1;
    old_cy = -1;
}

static void paint_cursor(char *fb, struct fb_var_screeninfo *vi, int cx, int cy) {
    // Сохраняем пиксели, которые перекроем
    for (int j = 0; j < CUR_H; j++)
        for (int i = 0; i < CUR_W; i++)
            saved[j * CUR_W + i] = get_pixel(fb, vi, cx + i, cy + j);

    unsigned int black = make_pixel(vi, 0, 0, 0);
    unsigned int white = make_pixel(vi, 255, 255, 255);

    for (int j = 0; j < CUR_H; j++) {
        for (int i = 0; i < CUR_W; i++) {
            char c = cursor_bitmap[j][i];
            if (c == 'X')      put_pixel(fb, vi, cx + i, cy + j, black);
            else if (c == '.') put_pixel(fb, vi, cx + i, cy + j, white);
        }
    }
    old_cx = cx;
    old_cy = cy;
}

int main(void) {
    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    int fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd < 0) { perror("/dev/fb0"); return 1; }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
        ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        perror("ioctl"); return 1;
    }
    g_line_length = finfo.line_length;
    long screensize = finfo.line_length * vinfo.yres;
    char *fb = mmap(NULL, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if (fb == MAP_FAILED) { perror("mmap"); return 1; }

    // Блокирующее открытие: ядро само разбудит нас, как только придёт пакет.
    // Никакого O_NONBLOCK и usleep — курсор обновляется ровно в темпе мыши.
    int mfd = open("/dev/input/mice", O_RDONLY);
    if (mfd < 0) { perror("/dev/input/mice"); return 1; }

    int cx = vinfo.xres / 2;
    int cy = vinfo.yres / 2;
    paint_cursor(fb, &vinfo, cx, cy);

    printf("Курсор в %dx%d. Двигай мышь. Ctrl+C — выход.\n",
           vinfo.xres, vinfo.yres);
    fflush(stdout);

    // /dev/input/mice: пакеты по 3 байта.
    //   data[0] — кнопки
    //   data[1] — dx (signed)
    //   data[2] — dy (signed), но с обратным знаком относительно экрана
    unsigned char data[3];
    while (g_running) {
        ssize_t n = read(mfd, data, 3);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("read");
            break;
        }
        if (n != 3) break;

        cx += (int)(signed char)data[1];
        cy -= (int)(signed char)data[2];

        // Клэмп
        if (cx < 0) cx = 0;
        if (cy < 0) cy = 0;
        if (cx > (int)vinfo.xres - CUR_W) cx = vinfo.xres - CUR_W;
        if (cy > (int)vinfo.yres - CUR_H) cy = vinfo.yres - CUR_H;

        // Перерисовываем на каждое событие
        erase_cursor(fb, &vinfo);
        paint_cursor(fb, &vinfo, cx, cy);
    }

    erase_cursor(fb, &vinfo);
    munmap(fb, screensize);
    close(fbfd);
    close(mfd);
    return 0;
}
