#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"

// ================= TFT CONFIG =================
#define TFT_SPI_PORT spi0
#define TFT_SCK  2
#define TFT_MOSI 3
#define TFT_MISO 4
#define TFT_CS   5
#define TFT_DC   1
#define TFT_RST  0

#define W 240
#define H 320

// ================= PROJECT PINS =================
#define HAPTIC_PIN 9

// Current hardware state: one EMG sensor connected on GPIO40/ADC0
#define EMG_FOREARM_PIN 40

// ================= COLORS =================
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define CYAN    0x07FF
#define DKGRAY  0x4208

// ================= TIMING =================
#define BUZZ_MS           220
#define MATCH_WINDOW_MS   350
#define SAMPLE_PERIOD_MS  5
#define SCORE_SHOW_MS     4000

// ================= EMG CONFIG =================
// IMPORTANT:
// If your EMG sensor can reach 4V, scale it down before the ADC.
// Example divider assumption:
// ADC_voltage = sensor_voltage * 0.6667
#define ADC_VREF                3.3f
#define ADC_MAX_COUNTS          4095.0f
#define EMG_SENSOR_TO_ADC_RATIO 0.6667f

#define SENSOR_THRESHOLD_VOLTS  3.5f
#define SENSOR_MIN_VOLTS        2.0f
#define SENSOR_MAX_VOLTS        4.0f

#define EMG_ALPHA               0.25f

// Set to 1 if you want to test without EMG connected
#define DEBUG_NO_EMG            0

#define MAX_EVENTS              16

typedef enum {
    REGION_FOREARM = 0,
    REGION_UNKNOWN = 255
} region_t;

typedef struct {
    uint32_t start_ms;
    uint32_t duration_ms;
    region_t region;
} pattern_event_t;

typedef struct {
    pattern_event_t events[MAX_EVENTS];
    size_t count;
} pattern_t;

// ================= ILI9341 =================
#define ILI9341_SWRESET 0x01
#define ILI9341_SLPOUT  0x11
#define ILI9341_DISPON  0x29
#define ILI9341_CASET   0x2A
#define ILI9341_PASET   0x2B
#define ILI9341_RAMWR   0x2C
#define ILI9341_MADCTL  0x36
#define ILI9341_COLMOD  0x3A

// ================= GLOBALS =================
static uint haptic_slice_num;
static float emg_filtered = 0.0f;

// ================= TFT LOW LEVEL =================
static void tft_cmd(uint8_t cmd) {
    gpio_put(TFT_DC, 0);
    gpio_put(TFT_CS, 0);
    spi_write_blocking(TFT_SPI_PORT, &cmd, 1);
    gpio_put(TFT_CS, 1);
}

static void tft_data(const uint8_t *d, size_t len) {
    gpio_put(TFT_DC, 1);
    gpio_put(TFT_CS, 0);
    spi_write_blocking(TFT_SPI_PORT, d, len);
    gpio_put(TFT_CS, 1);
}

static void tft_data8(uint8_t v) {
    tft_data(&v, 1);
}

static void tft_init(void) {
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
    tft_data8(0x55);   // RGB565

    tft_cmd(ILI9341_MADCTL);
    tft_data8(0x40);

    tft_cmd(ILI9341_DISPON);
    sleep_ms(100);
}

static void tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    tft_cmd(ILI9341_CASET);
    uint8_t c[] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
    tft_data(c, 4);

    tft_cmd(ILI9341_PASET);
    uint8_t r[] = {y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF};
    tft_data(r, 4);

    tft_cmd(ILI9341_RAMWR);
}

static void tft_pixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    tft_set_window(x, y, x, y);
    uint8_t d[] = {color >> 8, color & 0xFF};
    tft_data(d, 2);
}

static void clear_screen(uint16_t color) {
    tft_set_window(0, 0, W - 1, H - 1);
    uint8_t d[] = {color >> 8, color & 0xFF};
    for (int i = 0; i < W * H; i++) {
        tft_data(d, 2);
    }
}

static void clear_rect(int x0, int y0, int x1, int y1) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= W) x1 = W - 1;
    if (y1 >= H) y1 = H - 1;

<<<<<<< HEAD
    // All your code goes above.
    ////////////////////////////////////
}
=======
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            tft_pixel(x, y, BLACK);
        }
    }
}

// ================= DRAW PRIMITIVES =================
static void line(int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1) {
        tft_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void bezier(int x0, int y0, int cx, int cy, int x1, int y1, uint16_t c) {
    int px = x0, py = y0;
    for (int i = 1; i <= 24; i++) {
        float t = i / 24.0f;
        float it = 1.0f - t;
        int x = (int)(it * it * x0 + 2 * it * t * cx + t * t * x1);
        int y = (int)(it * it * y0 + 2 * it * t * cy + t * t * y1);
        line(px, py, x, y, c);
        px = x;
        py = y;
    }
}

static void fill_ellipse(int cx, int cy, int rx, int ry, uint16_t color) {
    for (int y = -ry; y <= ry; y++) {
        for (int x = -rx; x <= rx; x++) {
            float v = (x * x) / ((float)(rx * rx) + 1.0f) +
                      (y * y) / ((float)(ry * ry) + 1.0f);
            if (v < 1.0f) {
                tft_pixel(cx + x, cy + y, color);
            }
        }
    }
}

// ================= 3x5 DIGIT FONT =================
static const uint8_t digits3x5[10][5] = {
    {0x7, 0x5, 0x5, 0x5, 0x7},
    {0x2, 0x2, 0x2, 0x2, 0x2},
    {0x7, 0x4, 0x7, 0x1, 0x7},
    {0x7, 0x4, 0x7, 0x4, 0x7},
    {0x5, 0x5, 0x7, 0x4, 0x4},
    {0x7, 0x1, 0x7, 0x4, 0x7},
    {0x7, 0x1, 0x7, 0x5, 0x7},
    {0x7, 0x4, 0x4, 0x4, 0x4},
    {0x7, 0x5, 0x7, 0x5, 0x7},
    {0x7, 0x5, 0x7, 0x4, 0x7}
};

static void draw_digit(int x, int y, int d, uint16_t c, int scale) {
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 3; col++) {
            if (digits3x5[d][row] & (1 << col)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        tft_pixel(x + col * scale + sx, y + row * scale + sy, c);
                    }
                }
            }
        }
    }
}

static void draw_number(int x, int y, int value, uint16_t c, int scale) {
    if (value < 0) value = 0;
    if (value > 999) value = 999;

    int h = value / 100;
    int t = (value / 10) % 10;
    int o = value % 10;

    if (h > 0) {
        draw_digit(x, y, h, c, scale);
        x += 4 * scale;
    }
    draw_digit(x, y, t, c, scale);
    x += 4 * scale;
    draw_digit(x, y, o, c, scale);
}

static void draw_percent_symbol(int x, int y, uint16_t c, int scale) {
    fill_ellipse(x + 2 * scale, y + 2 * scale, scale, scale, c);
    fill_ellipse(x + 8 * scale, y + 8 * scale, scale, scale, c);
    line(x + 1 * scale, y + 9 * scale, x + 9 * scale, y + 1 * scale, c);
    line(x + 2 * scale, y + 9 * scale, x + 10 * scale, y + 1 * scale, c);
}

// ================= BODY DRAW =================
static void draw_body_outline(void) {
    int cx = 120;

    for (int y = -16; y <= 16; y++) {
        for (int x = -13; x <= 13; x++) {
            if (x * x * 16 + y * y * 10 <= 13 * 13 * 16) {
                tft_pixel(cx + x, 28 + y, WHITE);
            }
        }
    }

    bezier(cx - 4, 44, cx - 7, 50, cx - 5, 58, WHITE);
    bezier(cx + 4, 44, cx + 7, 50, cx + 5, 58, WHITE);

    bezier(cx - 5, 58, cx - 30, 50, cx - 48, 70, WHITE);
    bezier(cx + 5, 58, cx + 30, 52, cx + 48, 72, WHITE);

    bezier(cx - 48, 70, cx - 55, 90, cx - 42, 105, WHITE);
    bezier(cx - 42, 105, cx - 28, 120, cx - 30, 135, WHITE);
    bezier(cx - 30, 135, cx - 36, 150, cx - 40, 160, WHITE);

    bezier(cx + 48, 72, cx + 55, 92, cx + 42, 105, WHITE);
    bezier(cx + 42, 105, cx + 28, 120, cx + 30, 135, WHITE);
    bezier(cx + 30, 135, cx + 36, 150, cx + 40, 160, WHITE);

    for (int y = 60; y < 155; y += 3) {
        tft_pixel(cx, y, DKGRAY);
    }

    bezier(cx - 48, 70, cx - 62, 90, cx - 62, 115, WHITE);
    bezier(cx - 62, 115, cx - 64, 140, cx - 60, 162, WHITE);
    bezier(cx - 38, 78, cx - 48, 100, cx - 48, 118, WHITE);
    bezier(cx - 48, 118, cx - 50, 140, cx - 48, 160, WHITE);
    bezier(cx - 60, 162, cx - 54, 168, cx - 48, 160, WHITE);

    bezier(cx + 48, 72, cx + 62, 92, cx + 62, 115, WHITE);
    bezier(cx + 62, 115, cx + 64, 140, cx + 60, 162, WHITE);
    bezier(cx + 38, 78, cx + 48, 100, cx + 48, 118, WHITE);
    bezier(cx + 48, 118, cx + 50, 140, cx + 48, 160, WHITE);
    bezier(cx + 60, 162, cx + 54, 168, cx + 48, 160, WHITE);

    line(cx - 40, 160, cx + 40, 160, WHITE);
    line(cx - 3, 168, cx + 3, 168, WHITE);

    bezier(cx - 40, 160, cx - 34, 205, cx - 28, 245, WHITE);
    bezier(cx - 28, 245, cx - 24, 275, cx - 22, 280, WHITE);
    bezier(cx - 3, 168, cx - 10, 210, cx - 12, 250, WHITE);
    bezier(cx - 12, 250, cx - 13, 270, cx - 14, 280, WHITE);
    line(cx - 22, 280, cx - 14, 280, WHITE);

    bezier(cx + 40, 160, cx + 34, 205, cx + 28, 245, WHITE);
    bezier(cx + 28, 245, cx + 24, 275, cx + 22, 280, WHITE);
    bezier(cx + 3, 168, cx + 10, 210, cx + 12, 250, WHITE);
    bezier(cx + 12, 250, cx + 13, 270, cx + 14, 280, WHITE);
    line(cx + 22, 280, cx + 14, 280, WHITE);
}

// ================= FOREARM REGION ONLY =================
static void draw_forearm_region(uint16_t color) {
    int cx = 120;
    fill_ellipse(cx + 56, 140, 10, 20, color);
}

static uint16_t heat_red_from_brightness(uint8_t b) {
    if (b == 0) return BLACK;
    uint8_t r5 = (b * 31) / 255;
    return (uint16_t)(r5 << 11);
}

static void draw_base_scene(void) {
    clear_screen(BLACK);
    draw_body_outline();
}

static void draw_forearm_overlay(uint8_t brightness) {
    draw_base_scene();
    if (brightness > 0) {
        draw_forearm_region(heat_red_from_brightness(brightness));
    }
    draw_body_outline();
}

static void draw_score_screen(int score, bool pass) {
    clear_screen(BLACK);
    draw_body_outline();

    for (int y = 105; y <= 205; y++) {
        for (int x = 35; x <= 205; x++) {
            tft_pixel(x, y, BLACK);
        }
    }

    line(35, 105, 205, 105, WHITE);
    line(205, 105, 205, 205, WHITE);
    line(205, 205, 35, 205, WHITE);
    line(35, 205, 35, 105, WHITE);

    int scale = 7;
    int x = 62;
    int y = 128;

    if (score >= 100) {
        draw_digit(x, y, 1, CYAN, scale);
        x += 4 * scale;
        draw_digit(x, y, 0, CYAN, scale);
        x += 4 * scale;
        draw_digit(x, y, 0, CYAN, scale);
        x += 5 * scale;
    } else {
        int tens = (score / 10) % 10;
        int ones = score % 10;

        draw_digit(x, y, tens, CYAN, scale);
        x += 4 * scale;
        draw_digit(x, y, ones, CYAN, scale);
        x += 5 * scale;
    }

    draw_percent_symbol(x, y + 8, CYAN, 2);

    uint16_t bar_color = pass ? RED : DKGRAY;
    for (int yy = 178; yy <= 190; yy++) {
        for (int xx = 60; xx <= 180; xx++) {
            tft_pixel(xx, yy, bar_color);
        }
    }
}

// ================= HAPTIC =================
static void haptic_init(void) {
    gpio_set_function(HAPTIC_PIN, GPIO_FUNC_PWM);
    haptic_slice_num = pwm_gpio_to_slice_num(HAPTIC_PIN);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 4.0f);
    pwm_config_set_wrap(&cfg, 1000);
    pwm_init(haptic_slice_num, &cfg, true);

    pwm_set_gpio_level(HAPTIC_PIN, 0);
}

static void haptic_set_strength(uint16_t duty) {
    if (duty > 1000) duty = 1000;
    pwm_set_gpio_level(HAPTIC_PIN, duty);
}

static void haptic_buzz(uint16_t strength, uint32_t duration_ms) {
    haptic_set_strength(strength);
    sleep_ms(duration_ms);
    haptic_set_strength(0);
}

// ================= EMG =================
static inline float adc_counts_to_volts(uint16_t counts) {
    return ((float)counts * ADC_VREF) / ADC_MAX_COUNTS;
}

static void emg_adc_init(void) {
    adc_init();
    adc_gpio_init(EMG_FOREARM_PIN);
}

static uint16_t read_adc_channel(uint input) {
    adc_select_input(input);
    return adc_read();
}

static float read_sensor_voltage_raw(void) {
#if DEBUG_NO_EMG
    return 0.0f;
#else
    uint16_t raw = read_adc_channel(0); // GPIO40 = ADC0
    float adc_v = adc_counts_to_volts(raw);
    return adc_v / EMG_SENSOR_TO_ADC_RATIO;
#endif
}

static float read_sensor_voltage_filtered(void) {
    float raw = read_sensor_voltage_raw();
    emg_filtered = EMG_ALPHA * raw + (1.0f - EMG_ALPHA) * emg_filtered;
    return emg_filtered;
}

static bool forearm_active(void) {
    float v = read_sensor_voltage_filtered();
    return v >= SENSOR_THRESHOLD_VOLTS;
}

static uint8_t forearm_brightness(void) {
    float v = read_sensor_voltage_filtered();
    float x = (v - SENSOR_MIN_VOLTS) / (SENSOR_MAX_VOLTS - SENSOR_MIN_VOLTS);

    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;

    if (x < 0.15f) return 0;
    return (uint8_t)(x * 255.0f);
}

// ================= PATTERN =================
static void build_default_pattern(pattern_t *p) {
    p->count = 5;
    p->events[0] = (pattern_event_t){ .start_ms = 0,    .duration_ms = BUZZ_MS, .region = REGION_FOREARM };
    p->events[1] = (pattern_event_t){ .start_ms = 450,  .duration_ms = BUZZ_MS, .region = REGION_FOREARM };
    p->events[2] = (pattern_event_t){ .start_ms = 900,  .duration_ms = BUZZ_MS, .region = REGION_FOREARM };
    p->events[3] = (pattern_event_t){ .start_ms = 1450, .duration_ms = BUZZ_MS, .region = REGION_FOREARM };
    p->events[4] = (pattern_event_t){ .start_ms = 2000, .duration_ms = BUZZ_MS, .region = REGION_FOREARM };
}

// ================= RESULT HAPTIC =================
static void show_result_pattern(bool pass) {
    if (pass) {
        haptic_buzz(900, 180);
        sleep_ms(80);
        haptic_buzz(900, 180);
    } else {
        haptic_buzz(300, 500);
    }
}

// ================= PLAYBACK =================
static void play_pattern(const pattern_t *p) {
    absolute_time_t t0 = get_absolute_time();

    for (size_t i = 0; i < p->count; i++) {
        uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - to_ms_since_boot(t0);
        if (p->events[i].start_ms > elapsed) {
            sleep_ms(p->events[i].start_ms - elapsed);
        }

        draw_forearm_overlay(255);
        haptic_buzz(700, p->events[i].duration_ms);
        draw_base_scene();
    }
}

// ================= MATCHING =================
static bool wait_until_released(uint32_t timeout_ms) {
    absolute_time_t t0 = get_absolute_time();
    while ((to_ms_since_boot(get_absolute_time()) - to_ms_since_boot(t0)) < timeout_ms) {
        if (!forearm_active()) return true;
        sleep_ms(SAMPLE_PERIOD_MS);
    }
    return false;
}

static bool wait_for_forearm_activation(uint32_t window_ms, uint32_t *detected_ms) {
    absolute_time_t t0 = get_absolute_time();

    while ((to_ms_since_boot(get_absolute_time()) - to_ms_since_boot(t0)) < window_ms) {
        if (forearm_active()) {
            *detected_ms = to_ms_since_boot(get_absolute_time()) - to_ms_since_boot(t0);

            while (forearm_active()) {
                sleep_ms(SAMPLE_PERIOD_MS);
            }
            return true;
        }
        sleep_ms(SAMPLE_PERIOD_MS);
    }

    return false;
}

static uint32_t run_mimic_mode(const pattern_t *p, bool *pass_out) {
    wait_until_released(1500);
    sleep_ms(400);

    absolute_time_t t0 = get_absolute_time();
    uint32_t total_error = 0;
    bool all_correct = true;

    for (size_t i = 0; i < p->count; i++) {
        uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - to_ms_since_boot(t0);
        if (p->events[i].start_ms > elapsed) {
            sleep_ms(p->events[i].start_ms - elapsed);
        }

        draw_forearm_overlay(100);

        uint32_t detected_ms = 0;
        bool matched = wait_for_forearm_activation(MATCH_WINDOW_MS, &detected_ms);

        if (!matched) {
            all_correct = false;
            total_error += MATCH_WINDOW_MS;
            printf("Missed forearm pulse %u\n", (unsigned)i);
        } else {
            total_error += detected_ms;

            uint8_t b = forearm_brightness();
            if (b < 80) b = 80;
            draw_forearm_overlay(b);

            haptic_buzz(220, 70);
            printf("Matched forearm pulse %u, error=%lu ms\n",
                   (unsigned)i, (unsigned long)detected_ms);
        }
    }

    uint32_t max_error = (uint32_t)p->count * MATCH_WINDOW_MS;
    if (total_error > max_error) total_error = max_error;

    uint32_t score = 100 - ((100 * total_error) / max_error);
    *pass_out = all_correct && (score >= 60);
    return score;
}

// ================= LIVE MODE =================
static void live_heatmap_loop(void) {
    while (1) {
        uint8_t b = forearm_brightness();
        draw_forearm_overlay(b);

        if (forearm_active()) {
            haptic_set_strength(180);
        } else {
            haptic_set_strength(0);
        }

        sleep_ms(70);
    }
}

// ================= MAIN =================
int main(void) {
    stdio_init_all();
    sleep_ms(1200);

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
    haptic_init();
    emg_adc_init();

    draw_base_scene();

    pattern_t pattern = {0};
    build_default_pattern(&pattern);

    printf("Forearm-only pattern loaded\n");
    for (size_t i = 0; i < pattern.count; i++) {
        printf("%u: forearm @ %lu ms for %lu ms\n",
               (unsigned)i,
               (unsigned long)pattern.events[i].start_ms,
               (unsigned long)pattern.events[i].duration_ms);
    }

    sleep_ms(800);

    // 1. Play target pattern
    play_pattern(&pattern);

    // 2. Matching mode
    bool pass = false;
    uint32_t score = run_mimic_mode(&pattern, &pass);

    // 3. Big centered score screen
    draw_score_screen((int)score, pass);
    show_result_pattern(pass);

    printf("Final score: %lu, pass=%u\n", (unsigned long)score, pass ? 1 : 0);

    sleep_ms(SCORE_SHOW_MS);

    // 4. Live forearm heatmap mode
    live_heatmap_loop();

    return 0;
}
>>>>>>> 173166d (362 4/14 TFT with completion)
