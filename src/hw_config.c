#include <assert.h>

#include "pico/stdlib.h"
#include "ff.h"
#include "diskio.h"
#include "sd_driver/hw_config.h"

static spi_t spis[] = {
    {
        .hw_inst = spi0,
        .miso_gpio = 4,
        .mosi_gpio = 3,
        .sck_gpio = 2,
        .baud_rate = 12 * 1000 * 1000
    }
};

static sd_card_t sd_cards[] = {
    {
        .pcName = "0:",
        .spi = &spis[0],
        .ss_gpio = 6,
        .use_card_detect = false,
        .card_detect_gpio = 0,
        .card_detected_true = 0
    }
};

size_t sd_get_num(void) {
    return count_of(sd_cards);
}

sd_card_t *sd_get_by_num(size_t num) {
    assert(num < sd_get_num());
    if (num < sd_get_num()) return &sd_cards[num];
    return NULL;
}

size_t spi_get_num(void) {
    return count_of(spis);
}

spi_t *spi_get_by_num(size_t num) {
    assert(num < spi_get_num());
    if (num < spi_get_num()) return &spis[num];
    return NULL;
}