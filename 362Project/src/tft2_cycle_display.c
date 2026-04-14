#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"


// ================= TFT CONFIG =================
#define TFT_SPI_PORT spi0
#define TFT_SCK 2
#define TFT_MOSI 3
#define TFT_MISO 4
#define TFT_CS 5
#define TFT_DC 1
#define TFT_RST 0

#define W 240
#define H 320

// ================= COLORS =================
#define BLACK 0x0000
#define WHITE 0xFFFF
#define RED 0xF800
#define CYAN 0x07FF
#define DKGRAY 0x4208
#define FRAME_MS 50

// ================= HEART RATE CONFIG =================

// ================= ILI9341 =================
#define ILI9341_SWRESET 0x01
#define ILI9341_SLPOUT 0x11
#define ILI9341_DISPON 0x29
#define ILI9341_CASET 0x2A
#define ILI9341_PASET 0x2B
#define ILI9341_RAMWR 0x2C
#define ILI9341_MADCTL 0x36
#define ILI9341_COLMOD 0x3A

static void tft_cmd(uint8_t cmd)
{
    gpio_put(TFT_DC, 0);
    gpio_put(TFT_CS, 0);
    spi_write_blocking(TFT_SPI_PORT, &cmd, 1);
    gpio_put(TFT_CS, 1);
}
static void tft_data(const uint8_t *d, size_t len)
{
    gpio_put(TFT_DC, 1);
    gpio_put(TFT_CS, 0);
    spi_write_blocking(TFT_SPI_PORT, d, len);
    gpio_put(TFT_CS, 1);
}
static void tft_data8(uint8_t v) { tft_data(&v, 1); }

static void tft_init(void)
{
    gpio_put(TFT_RST, 1);
    sleep_ms(5);
    gpio_put(TFT_RST, 0);
    sleep_ms(20);
    gpio_put(TFT_RST, 1);
    sleep_ms(150);
    tft_cmd(ILI9341_SWRESET);
    sleep_ms(150);
    tft_cmd(ILI9341_SLPOUT);
    sleep_ms(150);
    tft_cmd(ILI9341_COLMOD);
    tft_data8(0x55);
    tft_cmd(ILI9341_MADCTL);
    tft_data8(0x40);
    tft_cmd(ILI9341_DISPON);
    sleep_ms(100);
}

static void tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    tft_cmd(ILI9341_CASET);
    uint8_t c[] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
    tft_data(c, 4);
    tft_cmd(ILI9341_PASET);
    uint8_t r[] = {y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF};
    tft_data(r, 4);
    tft_cmd(ILI9341_RAMWR);
}

static void tft_pixel(int16_t x, int16_t y, uint16_t color)
{
    if (x < 0 || x >= W || y < 0 || y >= H)
        return;
    tft_set_window(x, y, x, y);
    uint8_t d[] = {color >> 8, color & 0xFF};
    tft_data(d, 2);
}

// ================= DRAW PRIMITIVES =================
static void line(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1)
    {
        tft_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

static void bezier(int x0, int y0, int cx, int cy, int x1, int y1, uint16_t c)
{
    int px = x0, py = y0;
    for (int i = 1; i <= 24; i++)
    {
        float t = i / 24.0f, it = 1 - t;
        int x = it * it * x0 + 2 * it * t * cx + t * t * x1;
        int y = it * it * y0 + 2 * it * t * cy + t * t * y1;
        line(px, py, x, y, c);
        px = x;
        py = y;
    }
}

// ================= 3x5 DIGIT FONT =================
static const uint8_t digits3x5[10][5] = {
    {0x7, 0x5, 0x5, 0x5, 0x7}, // 0
    {0x2, 0x2, 0x2, 0x2, 0x2}, // 1
    {0x7, 0x4, 0x7, 0x1, 0x7}, // 2
    {0x7, 0x4, 0x7, 0x4, 0x7}, // 3
    {0x5, 0x5, 0x7, 0x4, 0x4}, // 4
    {0x7, 0x1, 0x7, 0x4, 0x7}, // 5
    {0x7, 0x1, 0x7, 0x5, 0x7}, // 6
    {0x7, 0x4, 0x4, 0x4, 0x4}, // 7
    {0x7, 0x5, 0x7, 0x5, 0x7}, // 8
    {0x7, 0x5, 0x7, 0x4, 0x7}  // 9
};

static void draw_digit(int x, int y, int d, uint16_t c, int scale)
{
    for (int row = 0; row < 5; row++)
        for (int col = 0; col < 3; col++)
            if (digits3x5[d][row] & (1 << col)) // changed from (2-col) to col
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        tft_pixel(x + col * scale + sx, y + row * scale + sy, c);
}

static void draw_bpm_label(int bpm)
{
    int x = 8, y = 8, scale = 4; // top-left corner, 4x bigger
    int h = bpm / 100, t = (bpm / 10) % 10, o = bpm % 10;
    if (h > 0)
    {
        draw_digit(x, y, h, CYAN, scale);
        x += 4 * scale;
    }
    draw_digit(x, y, t, CYAN, scale);
    x += 4 * scale;
    draw_digit(x, y, o, CYAN, scale);
    x += 5 * scale;

    // "BPM" label below the number, smaller
    int lx = 8, ly = 8 + 6 * scale, ls = 2;
    // B
    for (int i = 0; i < 5; i++)
        for (int s = 0; s < ls; s++)
            tft_pixel(lx + s, ly + i * ls, CYAN);
    for (int s = 0; s < ls; s++)
    {
        tft_pixel(lx + ls + s, ly, CYAN);
        tft_pixel(lx + ls + s, ly + 2 * ls, CYAN);
        tft_pixel(lx + ls + s, ly + 4 * ls, CYAN);
    }
    lx += 4 * ls;
    // P
    for (int i = 0; i < 5; i++)
        for (int s = 0; s < ls; s++)
            tft_pixel(lx + s, ly + i * ls, CYAN);
    for (int s = 0; s < ls; s++)
    {
        tft_pixel(lx + ls + s, ly, CYAN);
        tft_pixel(lx + ls + s, ly + 2 * ls, CYAN);
    }
    for (int s = 0; s < ls; s++)
        tft_pixel(lx + 2 * ls + s, ly + ls, CYAN);
    lx += 4 * ls;
    // M
    for (int i = 0; i < 5; i++)
    {
        for (int s = 0; s < ls; s++)
        {
            tft_pixel(lx + s, ly + i * ls, CYAN);
            tft_pixel(lx + 4 * ls + s, ly + i * ls, CYAN);
        }
    }
    for (int s = 0; s < ls; s++)
    {
        tft_pixel(lx + ls + s, ly + ls, CYAN);
        tft_pixel(lx + 3 * ls + s, ly + ls, CYAN);
        tft_pixel(lx + 2 * ls + s, ly + 2 * ls, CYAN);
    }
}

// ================= BODY =================
static void draw_body(void)
{
    int cx = 120;

    // HEAD
    for (int y = -16; y <= 16; y++)
        for (int x = -13; x <= 13; x++)
            if (x * x * 16 + y * y * 10 <= 13 * 13 * 16)
                tft_pixel(cx + x, 28 + y, WHITE);

    // NECK
    bezier(cx - 4, 44, cx - 7, 50, cx - 5, 58, WHITE);
    bezier(cx + 4, 44, cx + 7, 50, cx + 5, 58, WHITE);

    // SHOULDERS
    bezier(cx - 5, 58, cx - 30, 50, cx - 48, 70, WHITE);
    bezier(cx + 5, 58, cx + 30, 52, cx + 48, 72, WHITE);

    // TORSO
    bezier(cx - 48, 70, cx - 55, 90, cx - 42, 105, WHITE);
    bezier(cx - 42, 105, cx - 28, 120, cx - 30, 135, WHITE);
    bezier(cx - 30, 135, cx - 36, 150, cx - 40, 160, WHITE);
    bezier(cx + 48, 72, cx + 55, 92, cx + 42, 105, WHITE);
    bezier(cx + 42, 105, cx + 28, 120, cx + 30, 135, WHITE);
    bezier(cx + 30, 135, cx + 36, 150, cx + 40, 160, WHITE);

    // CENTER LINE
    for (int y = 60; y < 155; y += 3)
        tft_pixel(cx, y, DKGRAY);

    // LEFT ARM
    bezier(cx - 48, 70, cx - 62, 90, cx - 62, 115, WHITE);
    bezier(cx - 62, 115, cx - 64, 140, cx - 60, 162, WHITE);
    bezier(cx - 38, 78, cx - 48, 100, cx - 48, 118, WHITE);
    bezier(cx - 48, 118, cx - 50, 140, cx - 48, 160, WHITE);
    bezier(cx - 60, 162, cx - 54, 168, cx - 48, 160, WHITE);

    // RIGHT ARM
    bezier(cx + 48, 72, cx + 62, 92, cx + 62, 115, WHITE);
    bezier(cx + 62, 115, cx + 64, 140, cx + 60, 162, WHITE);
    bezier(cx + 38, 78, cx + 48, 100, cx + 48, 118, WHITE);
    bezier(cx + 48, 118, cx + 50, 140, cx + 48, 160, WHITE);
    bezier(cx + 60, 162, cx + 54, 168, cx + 48, 160, WHITE);

    // HIP / CROTCH
    line(cx - 40, 160, cx + 40, 160, WHITE);
    line(cx - 3, 168, cx + 3, 168, WHITE);

    // LEFT LEG
    bezier(cx - 40, 160, cx - 34, 205, cx - 28, 245, WHITE);
    bezier(cx - 28, 245, cx - 24, 275, cx - 22, 280, WHITE);
    bezier(cx - 3, 168, cx - 10, 210, cx - 12, 250, WHITE);
    bezier(cx - 12, 250, cx - 13, 270, cx - 14, 280, WHITE);
    line(cx - 22, 280, cx - 14, 280, WHITE);

    // RIGHT LEG
    bezier(cx + 40, 160, cx + 34, 205, cx + 28, 245, WHITE);
    bezier(cx + 28, 245, cx + 24, 275, cx + 22, 280, WHITE);
    bezier(cx + 3, 168, cx + 10, 210, cx + 12, 250, WHITE);
    bezier(cx + 12, 250, cx + 13, 270, cx + 14, 280, WHITE);
    line(cx + 22, 280, cx + 14, 280, WHITE);
}

// ================= HIGHLIGHT =================
static void highlight(int cx, int cy, int rx, int ry, uint16_t color)
{
    for (int y = -ry; y <= ry; y++)
        for (int x = -rx; x <= rx; x++)
            if ((x * x) / (rx * rx + 1.0f) + (y * y) / (ry * ry + 1.0f) < 1)
                tft_pixel(cx + x, cy + y, color);
}

// ================= MAIN =================
int main()
{
    stdio_init_all();
    spi_init(TFT_SPI_PORT, 32000000);
    gpio_set_function(TFT_SCK, GPIO_FUNC_SPI);
    gpio_set_function(TFT_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(TFT_MISO, GPIO_FUNC_SPI);

    gpio_init(TFT_CS);
    gpio_set_dir(TFT_CS, GPIO_OUT);
    gpio_put(TFT_CS, 1);
    gpio_init(TFT_DC);
    gpio_set_dir(TFT_DC, GPIO_OUT);
    gpio_init(TFT_RST);
    gpio_set_dir(TFT_RST, GPIO_OUT);

    tft_init();

    // ADC setup for MyoWare ENV signal on GP40 (ADC channel 0)
    adc_init();
    adc_gpio_init(40);
    adc_select_input(0);

    // Clear screen
    tft_set_window(0, 0, W - 1, H - 1);
    for (int i = 0; i < W * H; i++)
    {
        uint8_t d[] = {0, 0};
        tft_data(d, 2);
    }

    draw_body();

    // Heartbeat state (now local to main, where it belongs)
    int current_bpm = 72;
    int blob_cx = 105, blob_cy = 95;
    float phase = 0.0f;
    float phase_step = (6.283f * FRAME_MS) / (60000.0f / current_bpm);
    int last_rx = 0, last_ry = 0;
    int frames_until_change = 60;

    draw_bpm_label(current_bpm);

    while (1)
    {
        // === Randomly change BPM every few seconds ===

        // === Heartbeat pulse ===
       // Read MyoWare ENV signal from ADC (12-bit, 0-4095)
    uint16_t raw = adc_read();
    printf("raw=%d\n", raw);

    // Convert to intensity 0.0-1.0
    // Tune EMG_BASELINE and EMG_MAX based on what you actually see
    #define EMG_BASELINE 200    // ADC value at rest (idle ENV)
    #define EMG_MAX      2500   // ADC value at full flex
    float intensity = 0.0f;
    if (raw > EMG_BASELINE) {
        intensity = (float)(raw - EMG_BASELINE) / (EMG_MAX - EMG_BASELINE);
        if (intensity > 1.0f) intensity = 1.0f;
    }

        // Erase previous blob
        if (last_rx > 0)
        {
            for (int y = -last_ry - 1; y <= last_ry + 1; y++)
                for (int x = -last_rx - 1; x <= last_rx + 1; x++)
                {
                    float v = (x * x) / ((float)((last_rx + 1) * (last_rx + 1))) + (y * y) / ((float)((last_ry + 1) * (last_ry + 1)));
                    if (v < 1.0f)
                        tft_pixel(blob_cx + x, blob_cy + y, BLACK);
                }
        }

        // Draw new blob
        if (intensity > 0.05f)
        {
            int rx = 5 + (int)(intensity * 10);
            int ry = 6 + (int)(intensity * 12);
            highlight(blob_cx, blob_cy, rx, ry, RED);
            last_rx = rx;
            last_ry = ry;
        }
        else
        {
            last_rx = 0;
            last_ry = 0;
        }

        sleep_ms(FRAME_MS);
    }
}