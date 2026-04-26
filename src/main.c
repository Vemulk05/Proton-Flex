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

#include "ff.h"

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

// ================= SD CONFIG =================
#define SD_CS 6

// ================= PROJECT PINS =================
#define HAPTIC_PIN 9

// RP2350B ADC mapping assumed:
// GPIO40 -> ADC0 -> forearm
// GPIO41 -> ADC1 -> bicep
// GPIO42 -> ADC2 -> quadricep
#define EMG_FOREARM_PIN      40
#define EMG_BICEP_PIN        41
#define EMG_QUADRICEP_PIN    42

// ================= COLORS =================
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define CYAN    0x07FF
#define DKGRAY  0x4208

// ================= TIMING =================
#define BUZZ_MS                 220
#define MATCH_WINDOW_MS         350
#define SAMPLE_PERIOD_MS        5
#define SCORE_SHOW_MS           4000
#define SCOREBOARD_SHOW_MS      3000
#define LIVE_FRAME_MS           70

// Random pattern timing
#define RANDOM_PATTERN_COUNT    5
#define MIN_EVENT_GAP_MS        420
#define MAX_EVENT_GAP_MS        720
#define MIN_EVENT_DUR_MS        170
#define MAX_EVENT_DUR_MS        260

// Classification robustness
#define CLASSIFY_MARGIN_VOLTS     0.10f
#define CLASSIFY_CONFIRM_SAMPLES  3

// ================= EMG CONFIG =================
#define ADC_VREF                3.3f
#define ADC_MAX_COUNTS          4095.0f
#define EMG_SENSOR_TO_ADC_RATIO 0.6667f

#define SENSOR_THRESHOLD_VOLTS  3.5f
#define SENSOR_MIN_VOLTS        2.0f
#define SENSOR_MAX_VOLTS        4.0f

#define EMG_ALPHA               0.25f
#define DEBUG_NO_EMG            0

#define MAX_EVENTS              16
#define TOP_SCORES_COUNT        5

typedef enum {
    REGION_FOREARM = 0,
    REGION_BICEP = 1,
    REGION_QUADRICEP = 2,
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

typedef struct {
    bool responded;
    bool correct_region;
    region_t detected_region;
    uint32_t detected_ms;
} event_eval_t;

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
static float emg_filtered[3] = {0.0f, 0.0f, 0.0f};
static int last_region_brightness[3] = {-1, -1, -1};
static FATFS fs;
static bool sd_ok = false;

// ================= TFT LOW LEVEL =================
static void tft_cmd(uint8_t cmd) {
    gpio_put(SD_CS, 1);   // keep SD deselected on shared SPI bus
    gpio_put(TFT_DC, 0);
    gpio_put(TFT_CS, 0);
    spi_write_blocking(TFT_SPI_PORT, &cmd, 1);
    gpio_put(TFT_CS, 1);
}

static void tft_data(const uint8_t *d, size_t len) {
    gpio_put(SD_CS, 1);   // keep SD deselected on shared SPI bus
    gpio_put(TFT_DC, 1);
    gpio_put(TFT_CS, 0);
    spi_write_blocking(TFT_SPI_PORT, d, len);
    gpio_put(TFT_CS, 1);
}

static void tft_data8(uint8_t v) {
    tft_data(&v, 1);
}

static void tft_restore_spi(void) {
    spi_init(TFT_SPI_PORT, 32000000);
    gpio_set_function(TFT_SCK, GPIO_FUNC_SPI);
    gpio_set_function(TFT_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(TFT_MISO, GPIO_FUNC_SPI);
    gpio_put(TFT_CS, 1);
    gpio_put(SD_CS, 1);
}

static void tft_init(void) {
    gpio_put(SD_CS, 1);
    gpio_put(TFT_CS, 1);

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
    if (d < 0 || d > 9) return;
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
    char buf[8];
    if (value < 0) value = 0;
    if (value > 999) value = 999;

    snprintf(buf, sizeof(buf), "%d", value);

    for (int i = 0; buf[i] != '\0'; i++) {
        draw_digit(x, y, buf[i] - '0', c, scale);
        x += 4 * scale;
    }
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

    // head
    for (int y = -16; y <= 16; y++) {
        for (int x = -13; x <= 13; x++) {
            if (x * x * 16 + y * y * 10 <= 13 * 13 * 16) {
                tft_pixel(cx + x, 28 + y, WHITE);
            }
        }
    }

    // neck
    bezier(cx - 5, 44, cx - 6, 52, cx - 5, 60, WHITE);
    bezier(cx + 5, 44, cx + 6, 52, cx + 5, 60, WHITE);

    // shoulder caps
    bezier(cx - 5, 60, cx - 18, 63, cx - 34, 72, WHITE);
    bezier(cx + 5, 60, cx + 18, 63, cx + 34, 72, WHITE);

    // torso side walls
    bezier(cx - 34, 72, cx - 36, 112, cx - 30, 160, WHITE);
    bezier(cx + 34, 72, cx + 36, 112, cx + 30, 160, WHITE);

    // connect shoulder underside to arm inner edge
    line(cx - 34, 72, cx - 24, 76, WHITE);
    line(cx + 34, 72, cx + 24, 76, WHITE);

    // center line
    for (int y = 64; y < 155; y += 3) {
        tft_pixel(cx, y, DKGRAY);
    }

    // left arm outer
    bezier(cx - 34, 72, cx - 54, 82, cx - 62, 112, WHITE);
    bezier(cx - 62, 112, cx - 66, 142, cx - 58, 166, WHITE);

    // left arm inner
    bezier(cx - 24, 76, cx - 42, 88, cx - 48, 116, WHITE);
    bezier(cx - 48, 116, cx - 50, 144, cx - 46, 166, WHITE);

    // left wrist close
    bezier(cx - 58, 166, cx - 52, 172, cx - 46, 166, WHITE);

    // right arm outer
    bezier(cx + 34, 72, cx + 54, 82, cx + 62, 112, WHITE);
    bezier(cx + 62, 112, cx + 66, 142, cx + 58, 166, WHITE);

    // right arm inner
    bezier(cx + 24, 76, cx + 42, 88, cx + 48, 116, WHITE);
    bezier(cx + 48, 116, cx + 50, 144, cx + 46, 166, WHITE);

    // right wrist close
    bezier(cx + 58, 166, cx + 52, 172, cx + 46, 166, WHITE);

    // pelvis
    line(cx - 30, 160, cx + 30, 160, WHITE);
    line(cx - 4, 168, cx + 4, 168, WHITE);

    // left leg
    bezier(cx - 30, 160, cx - 28, 205, cx - 23, 246, WHITE);
    bezier(cx - 23, 246, cx - 19, 274, cx - 17, 280, WHITE);
    bezier(cx - 4, 168, cx - 10, 210, cx - 10, 250, WHITE);
    bezier(cx - 10, 250, cx - 11, 270, cx - 12, 280, WHITE);
    line(cx - 17, 280, cx - 12, 280, WHITE);

    // right leg
    bezier(cx + 30, 160, cx + 28, 205, cx + 23, 246, WHITE);
    bezier(cx + 23, 246, cx + 19, 274, cx + 17, 280, WHITE);
    bezier(cx + 4, 168, cx + 10, 210, cx + 10, 250, WHITE);
    bezier(cx + 10, 250, cx + 11, 270, cx + 12, 280, WHITE);
    line(cx + 17, 280, cx + 12, 280, WHITE);
}

// ================= REGION DRAW =================
static void draw_region_fill(region_t region, uint16_t color) {
    int cx = 120;

    switch (region) {
        case REGION_FOREARM:
            fill_ellipse(cx + 58, 142, 9, 20, color);
            break;
        case REGION_BICEP:
            fill_ellipse(cx + 55, 102, 11, 17, color);
            break;
        case REGION_QUADRICEP:
            fill_ellipse(cx + 18, 210, 14, 22, color);
            break;
        default:
            break;
    }
}

static uint16_t region_color_from_brightness(region_t region, uint8_t b) {
    if (b == 0) return BLACK;

    switch (region) {
        case REGION_FOREARM: {
            uint8_t r5 = (b * 31) / 255;
            return (uint16_t)(r5 << 11);
        }
        case REGION_BICEP: {
            uint8_t g6 = (b * 63) / 255;
            return (uint16_t)(g6 << 5);
        }
        case REGION_QUADRICEP: {
            uint8_t bl5 = (b * 31) / 255;
            return (uint16_t)(bl5);
        }
        default:
            return BLACK;
    }
}

static void clear_region_box(region_t region) {
    switch (region) {
        case REGION_FOREARM:
            clear_rect(164, 118, 188, 168);
            break;
        case REGION_BICEP:
            clear_rect(160, 82, 184, 124);
            break;
        case REGION_QUADRICEP:
            clear_rect(120, 186, 152, 234);
            break;
        default:
            break;
    }
}

static void reset_region_cache(void) {
    for (int i = 0; i < 3; i++) {
        last_region_brightness[i] = -1;
    }
}

static void draw_base_scene(void) {
    clear_screen(BLACK);
    draw_body_outline();
    reset_region_cache();
}

static void update_region_overlay(region_t region, uint8_t brightness) {
    if (region > REGION_QUADRICEP) return;

    int idx = (int)region;
    if (last_region_brightness[idx] >= 0) {
        int diff = (int)brightness - last_region_brightness[idx];
        if (diff < 0) diff = -diff;
        if (diff < 4) return;
    }

    clear_region_box(region);

    if (brightness > 0) {
        draw_region_fill(region, region_color_from_brightness(region, brightness));
    }

    draw_body_outline();
    last_region_brightness[idx] = brightness;
}

static void update_all_regions(uint8_t forearm_b, uint8_t bicep_b, uint8_t quadricep_b) {
    uint8_t vals[3] = { forearm_b, bicep_b, quadricep_b };
    bool changed = false;

    for (int idx = 0; idx < 3; idx++) {
        uint8_t brightness = vals[idx];

        if (last_region_brightness[idx] >= 0) {
            int diff = (int)brightness - last_region_brightness[idx];
            if (diff < 0) diff = -diff;
            if (diff < 4) continue;
        }

        region_t region = (region_t)idx;
        clear_region_box(region);

        if (brightness > 0) {
            draw_region_fill(region, region_color_from_brightness(region, brightness));
        }

        last_region_brightness[idx] = brightness;
        changed = true;
    }

    if (changed) {
        draw_body_outline();
    }
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

static void draw_top_scores_screen(const int scores[TOP_SCORES_COUNT], size_t count) {
    clear_screen(BLACK);

    // outer border
    line(25, 20, 215, 20, WHITE);
    line(215, 20, 215, 300, WHITE);
    line(215, 300, 25, 300, WHITE);
    line(25, 300, 25, 20, WHITE);

    // header divider
    line(25, 50, 215, 50, WHITE);

    // big "5" header
    draw_digit(112, 26, 5, CYAN, 4);

    for (size_t i = 0; i < count && i < TOP_SCORES_COUNT; i++) {
        int y = 65 + (int)i * 45;

        // rank
        draw_digit(45, y, (int)(i + 1), WHITE, 4);

        // separator
        line(78, y + 10, 92, y + 10, WHITE);

        // score
        draw_number(110, y, scores[i], CYAN, 4);
        draw_percent_symbol(160, y + 6, CYAN, 2);

        if (i < TOP_SCORES_COUNT - 1) {
            line(40, y + 34, 200, y + 34, DKGRAY);
        }
    }
}

static void flash_status(uint16_t color, int ms) {
    clear_screen(color);
    sleep_ms(ms);
    clear_screen(BLACK);
    draw_body_outline();
    reset_region_cache();
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

static inline int region_index(region_t region) {
    switch (region) {
        case REGION_FOREARM:   return 0;
        case REGION_BICEP:     return 1;
        case REGION_QUADRICEP: return 2;
        default:               return -1;
    }
}

static void emg_adc_init(void) {
    adc_init();
    adc_gpio_init(EMG_FOREARM_PIN);
    adc_gpio_init(EMG_BICEP_PIN);
    adc_gpio_init(EMG_QUADRICEP_PIN);
}

static uint16_t read_adc_channel(uint input) {
    adc_select_input(input);
    return adc_read();
}

static float read_sensor_voltage_raw(region_t region) {
#if DEBUG_NO_EMG
    (void)region;
    return 0.0f;
#else
    uint16_t raw = 0;

    switch (region) {
        case REGION_FOREARM:   raw = read_adc_channel(0); break;
        case REGION_BICEP:     raw = read_adc_channel(1); break;
        case REGION_QUADRICEP: raw = read_adc_channel(2); break;
        default: return 0.0f;
    }

    float adc_v = adc_counts_to_volts(raw);
    return adc_v / EMG_SENSOR_TO_ADC_RATIO;
#endif
}

static void sample_all_emg(void) {
    for (int r = 0; r < 3; r++) {
        float raw = read_sensor_voltage_raw((region_t)r);
        emg_filtered[r] = EMG_ALPHA * raw + (1.0f - EMG_ALPHA) * emg_filtered[r];
    }
}

static float read_sensor_voltage_filtered(region_t region) {
    int idx = region_index(region);
    if (idx < 0) return 0.0f;
    return emg_filtered[idx];
}

static bool region_active(region_t region) {
    return read_sensor_voltage_filtered(region) >= SENSOR_THRESHOLD_VOLTS;
}

static uint8_t region_brightness(region_t region) {
    float v = read_sensor_voltage_filtered(region);
    float x = (v - SENSOR_MIN_VOLTS) / (SENSOR_MAX_VOLTS - SENSOR_MIN_VOLTS);

    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;

    if (x < 0.15f) return 0;
    return (uint8_t)(x * 255.0f);
}

static region_t strongest_active_region(void) {
#if DEBUG_NO_EMG
    return REGION_UNKNOWN;
#else
    float forearm   = read_sensor_voltage_filtered(REGION_FOREARM);
    float bicep     = read_sensor_voltage_filtered(REGION_BICEP);
    float quadricep = read_sensor_voltage_filtered(REGION_QUADRICEP);

    float max_v = forearm;
    region_t best = REGION_FOREARM;

    if (bicep > max_v) {
        max_v = bicep;
        best = REGION_BICEP;
    }
    if (quadricep > max_v) {
        max_v = quadricep;
        best = REGION_QUADRICEP;
    }

    if (max_v >= SENSOR_THRESHOLD_VOLTS) return best;
    return REGION_UNKNOWN;
#endif
}

static region_t classify_region_strict(void) {
#if DEBUG_NO_EMG
    return REGION_UNKNOWN;
#else
    float vals[3] = {
        read_sensor_voltage_filtered(REGION_FOREARM),
        read_sensor_voltage_filtered(REGION_BICEP),
        read_sensor_voltage_filtered(REGION_QUADRICEP)
    };

    int best_idx = 0;
    int second_idx = 1;

    if (vals[second_idx] > vals[best_idx]) {
        int tmp = best_idx;
        best_idx = second_idx;
        second_idx = tmp;
    }

    for (int i = 2; i < 3; i++) {
        if (vals[i] > vals[best_idx]) {
            second_idx = best_idx;
            best_idx = i;
        } else if (vals[i] > vals[second_idx]) {
            second_idx = i;
        }
    }

    float best_v = vals[best_idx];
    float second_v = vals[second_idx];

    if (best_v < SENSOR_THRESHOLD_VOLTS) return REGION_UNKNOWN;
    if ((best_v - second_v) < CLASSIFY_MARGIN_VOLTS) return REGION_UNKNOWN;

    return (region_t)best_idx;
#endif
}

// ================= PATTERN + SD HELPERS =================
static const char *region_name(region_t region) {
    switch (region) {
        case REGION_FOREARM:   return "FOREARM";
        case REGION_BICEP:     return "BICEP";
        case REGION_QUADRICEP: return "QUADRICEP";
        default:               return "UNKNOWN";
    }
}

static region_t region_from_string(const char *s) {
    if (strcmp(s, "FOREARM") == 0)   return REGION_FOREARM;
    if (strcmp(s, "BICEP") == 0)     return REGION_BICEP;
    if (strcmp(s, "QUADRICEP") == 0) return REGION_QUADRICEP;
    return REGION_UNKNOWN;
}

static uint32_t rand_range_u32(uint32_t lo, uint32_t hi) {
    if (hi <= lo) return lo;
    return lo + (uint32_t)(rand() % (int)(hi - lo + 1));
}

static void seed_random_pattern(void) {
    uint32_t seed = (uint32_t)to_ms_since_boot(get_absolute_time());

#if !DEBUG_NO_EMG
    seed ^= ((uint32_t)read_adc_channel(0) << 0);
    seed ^= ((uint32_t)read_adc_channel(1) << 10);
    seed ^= ((uint32_t)read_adc_channel(2) << 20);
#endif

    if (seed == 0) seed = 1;
    srand(seed);
}

static region_t random_region_no_triple(region_t prev1, region_t prev2) {
    region_t r;
    do {
        r = (region_t)(rand() % 3);
    } while (prev1 == prev2 && r == prev1);
    return r;
}

static void build_random_pattern(pattern_t *p) {
    p->count = RANDOM_PATTERN_COUNT;

    uint32_t t = 0;
    region_t prev1 = REGION_UNKNOWN;
    region_t prev2 = REGION_UNKNOWN;

    for (size_t i = 0; i < p->count; i++) {
        region_t r = random_region_no_triple(prev1, prev2);
        uint32_t dur = rand_range_u32(MIN_EVENT_DUR_MS, MAX_EVENT_DUR_MS);

        p->events[i].start_ms = t;
        p->events[i].duration_ms = dur;
        p->events[i].region = r;

        t += rand_range_u32(MIN_EVENT_GAP_MS, MAX_EVENT_GAP_MS);

        prev2 = prev1;
        prev1 = r;
    }
}

static bool save_pattern_csv(const char *path, const pattern_t *p) {
    FIL fil;
    UINT bw;

    FRESULT fr = f_open(&fil, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) return false;

    const char *hdr = "start_ms,duration_ms,region\r\n";
    if (f_write(&fil, hdr, (UINT)strlen(hdr), &bw) != FR_OK) {
        f_close(&fil);
        return false;
    }

    char line[96];
    for (size_t i = 0; i < p->count; i++) {
        int n = snprintf(line, sizeof(line), "%lu,%lu,%s\r\n",
                         (unsigned long)p->events[i].start_ms,
                         (unsigned long)p->events[i].duration_ms,
                         region_name(p->events[i].region));
        if (f_write(&fil, line, (UINT)n, &bw) != FR_OK) {
            f_close(&fil);
            return false;
        }
    }

    f_close(&fil);
    return true;
}

static bool load_pattern_csv(const char *path, pattern_t *p) {
    FIL fil;
    char line[96];

    p->count = 0;
    FRESULT fr = f_open(&fil, path, FA_READ);
    if (fr != FR_OK) return false;

    if (!f_gets(line, sizeof(line), &fil)) {
        f_close(&fil);
        return false;
    }

    while (f_gets(line, sizeof(line), &fil) && p->count < MAX_EVENTS) {
        unsigned long start_ms = 0;
        unsigned long duration_ms = 0;
        char region_s[24] = {0};

        if (sscanf(line, "%lu,%lu,%23s", &start_ms, &duration_ms, region_s) == 3) {
            p->events[p->count].start_ms = (uint32_t)start_ms;
            p->events[p->count].duration_ms = (uint32_t)duration_ms;
            p->events[p->count].region = region_from_string(region_s);
            if (p->events[p->count].region != REGION_UNKNOWN) {
                p->count++;
            }
        }
    }

    f_close(&fil);
    return p->count > 0;
}

static uint32_t get_next_user_index(const char *path) {
    FIL fil;
    char line[96];
    uint32_t idx = 1;

    FRESULT fr = f_open(&fil, path, FA_READ);
    if (fr != FR_OK) return 1;

    // skip header
    f_gets(line, sizeof(line), &fil);

    while (f_gets(line, sizeof(line), &fil)) {
        if (strlen(line) > 2) idx++;
    }

    f_close(&fil);
    return idx;
}

static void append_attempt_csv(const char *path, uint32_t score) {
    FIL fil;
    UINT bw;
    uint32_t user_idx = get_next_user_index(path);

    FRESULT fr = f_open(&fil, path, FA_OPEN_APPEND | FA_WRITE);
    if (fr != FR_OK) {
        fr = f_open(&fil, path, FA_CREATE_ALWAYS | FA_WRITE);
        if (fr != FR_OK) return;

        const char *hdr = "user,score\r\n";
        f_write(&fil, hdr, (UINT)strlen(hdr), &bw);
        user_idx = 1;
    }

    char line[64];
    int n = snprintf(line, sizeof(line), "User %lu,%lu\r\n",
                     (unsigned long)user_idx,
                     (unsigned long)score);
    f_write(&fil, line, (UINT)n, &bw);
    f_close(&fil);
}

static size_t load_top_scores_csv(const char *path, int out_scores[TOP_SCORES_COUNT]) {
    FIL fil;
    char line[96];

    for (int i = 0; i < TOP_SCORES_COUNT; i++) {
        out_scores[i] = -1;
    }

    FRESULT fr = f_open(&fil, path, FA_READ);
    if (fr != FR_OK) return 0;

    // skip header
    if (!f_gets(line, sizeof(line), &fil)) {
        f_close(&fil);
        return 0;
    }

    while (f_gets(line, sizeof(line), &fil)) {
        char *comma = strrchr(line, ',');
        if (!comma) continue;

        int score = atoi(comma + 1);
        if (score < 0) score = 0;
        if (score > 100) score = 100;

        for (size_t pos = 0; pos < TOP_SCORES_COUNT; pos++) {
            if (out_scores[pos] < score) {
                for (size_t j = TOP_SCORES_COUNT - 1; j > pos; j--) {
                    out_scores[j] = out_scores[j - 1];
                }
                out_scores[pos] = score;
                break;
            } else if (out_scores[pos] == -1) {
                out_scores[pos] = score;
                break;
            }
        }
    }

    f_close(&fil);

    size_t count = 0;
    for (int i = 0; i < TOP_SCORES_COUNT; i++) {
        if (out_scores[i] >= 0) count++;
    }

    return count;
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
    draw_base_scene();

    absolute_time_t t0 = get_absolute_time();

    for (size_t i = 0; i < p->count; i++) {
        uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - to_ms_since_boot(t0);
        if (p->events[i].start_ms > elapsed) {
            sleep_ms(p->events[i].start_ms - elapsed);
        }

        update_region_overlay(p->events[i].region, 255);
        haptic_buzz(700, p->events[i].duration_ms);
        update_region_overlay(p->events[i].region, 0);
    }
}

// ================= MATCHING =================
static bool wait_until_released(uint32_t timeout_ms) {
    absolute_time_t t0 = get_absolute_time();

    while ((to_ms_since_boot(get_absolute_time()) - to_ms_since_boot(t0)) < timeout_ms) {
        sample_all_emg();
        bool any_active =
            region_active(REGION_FOREARM) ||
            region_active(REGION_BICEP) ||
            region_active(REGION_QUADRICEP);

        if (!any_active) return true;
        sleep_ms(SAMPLE_PERIOD_MS);
    }

    return false;
}

static event_eval_t wait_for_response(region_t expected, uint32_t window_ms) {
    event_eval_t ev = {
        .responded = false,
        .correct_region = false,
        .detected_region = REGION_UNKNOWN,
        .detected_ms = window_ms
    };

    absolute_time_t t0 = get_absolute_time();
    region_t candidate = REGION_UNKNOWN;
    int confirm_count = 0;

    while ((to_ms_since_boot(get_absolute_time()) - to_ms_since_boot(t0)) < window_ms) {
        sample_all_emg();

        region_t r = classify_region_strict();

        if (r == REGION_UNKNOWN) {
            candidate = REGION_UNKNOWN;
            confirm_count = 0;
        } else {
            if (r == candidate) {
                confirm_count++;
            } else {
                candidate = r;
                confirm_count = 1;
            }

            if (confirm_count >= CLASSIFY_CONFIRM_SAMPLES) {
                ev.responded = true;
                ev.detected_region = r;
                ev.detected_ms = to_ms_since_boot(get_absolute_time()) - to_ms_since_boot(t0);
                ev.correct_region = (r == expected);

                while (1) {
                    sample_all_emg();
                    if (strongest_active_region() == REGION_UNKNOWN) break;
                    sleep_ms(SAMPLE_PERIOD_MS);
                }
                return ev;
            }
        }

        sleep_ms(SAMPLE_PERIOD_MS);
    }

    return ev;
}

static uint32_t run_mimic_mode(const pattern_t *p, bool *pass_out) {
    wait_until_released(1500);
    sleep_ms(400);

    draw_base_scene();

    absolute_time_t t0 = get_absolute_time();
    uint32_t total_score = 0;
    size_t correct_count = 0;

    for (size_t i = 0; i < p->count; i++) {
        uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - to_ms_since_boot(t0);
        if (p->events[i].start_ms > elapsed) {
            sleep_ms(p->events[i].start_ms - elapsed);
        }

        update_region_overlay(p->events[i].region, 100);

        event_eval_t ev = wait_for_response(p->events[i].region, MATCH_WINDOW_MS);
        uint32_t event_score = 0;

        if (!ev.responded) {
            update_region_overlay(p->events[i].region, 0);
        } else if (!ev.correct_region) {
            update_region_overlay(p->events[i].region, 0);
        } else {
            event_score = 100 - ((100 * ev.detected_ms) / MATCH_WINDOW_MS);
            if (event_score > 100) event_score = 0;

            uint8_t b = region_brightness(p->events[i].region);
            if (b < 80) b = 80;
            update_region_overlay(p->events[i].region, b);

            haptic_buzz(220, 70);
            correct_count++;
        }

        total_score += event_score;
    }

    uint32_t score = total_score / p->count;
    *pass_out = (score >= 60) && (correct_count >= 7);

    return score;
}

// ================= LIVE MODE =================
static void live_heatmap_loop(void) {
    draw_base_scene();

    while (1) {
        sample_all_emg();

        uint8_t forearm_b = region_brightness(REGION_FOREARM);
        uint8_t bicep_b = region_brightness(REGION_BICEP);
        uint8_t quadricep_b = region_brightness(REGION_QUADRICEP);

        update_all_regions(forearm_b, bicep_b, quadricep_b);

        if (region_active(REGION_FOREARM) ||
            region_active(REGION_BICEP) ||
            region_active(REGION_QUADRICEP)) {
            haptic_set_strength(180);
        } else {
            haptic_set_strength(0);
        }

        sleep_ms(LIVE_FRAME_MS);
    }
}

static bool sd_mount_card(void) {
    gpio_put(TFT_CS, 1);
    gpio_put(SD_CS, 1);

    FRESULT fr = f_mount(&fs, "0:", 1);
    return fr == FR_OK;
}

// ================= MAIN =================
int main(void) {
    stdio_init_all();
    sleep_ms(1200);

    gpio_init(TFT_CS);
    gpio_set_dir(TFT_CS, GPIO_OUT);
    gpio_put(TFT_CS, 1);

    gpio_init(SD_CS);
    gpio_set_dir(SD_CS, GPIO_OUT);
    gpio_put(SD_CS, 1);

    gpio_init(TFT_DC);
    gpio_set_dir(TFT_DC, GPIO_OUT);

    gpio_init(TFT_RST);
    gpio_set_dir(TFT_RST, GPIO_OUT);

    tft_restore_spi();
    tft_init();
    haptic_init();
    emg_adc_init();
    draw_base_scene();

    pattern_t pattern = {0};
    int top_scores[TOP_SCORES_COUNT] = {0};
    size_t top_score_count = 0;

    seed_random_pattern();
    build_random_pattern(&pattern);

    sd_ok = sd_mount_card();
    if (sd_ok) {
        flash_status(GREEN, 180);

        // Save exact random pattern used this run
        save_pattern_csv("0:/pattern.csv", &pattern);

        // Re-load from SD so the SD copy is the source of truth
        pattern_t loaded = {0};
        if (load_pattern_csv("0:/pattern.csv", &loaded)) {
            pattern = loaded;
        }

        tft_restore_spi();
        gpio_put(SD_CS, 1);
        tft_init();
        draw_base_scene();
    } else {
        flash_status(RED, 250);
    }

    sleep_ms(800);

    play_pattern(&pattern);

    bool pass = false;
    uint32_t score = run_mimic_mode(&pattern, &pass);

    if (sd_ok) {
        append_attempt_csv("0:/attempts.csv", score);
        top_score_count = load_top_scores_csv("0:/attempts.csv", top_scores);

        tft_restore_spi();
        gpio_put(SD_CS, 1);
        tft_init();
        draw_base_scene();

        flash_status(GREEN, 120);
    }

    draw_score_screen((int)score, pass);
    show_result_pattern(pass);

    printf("Final score: %lu, pass=%u\n", (unsigned long)score, pass ? 1 : 0);

    sleep_ms(SCORE_SHOW_MS);

    if (sd_ok && top_score_count > 0) {
        draw_top_scores_screen(top_scores, top_score_count);
        sleep_ms(SCOREBOARD_SHOW_MS);
    }

    live_heatmap_loop();

    return 0;
}