#include <stdio.h>
#include "pico/stdlib.h"

//////////////////////////////////////////////////////////////////////////////

const char* username = "vemula2";

//////////////////////////////////////////////////////////////////////////////

void autotest();

void init_outputs() {
    // fill in

    //defining pins 22-25
    io_bank0_hw -> io[22].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    io_bank0_hw -> io[23].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    io_bank0_hw -> io[24].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    io_bank0_hw -> io[25].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;

    //clearing leds to known state
    sio_hw -> gpio_clr = (1u << 22) | (1u << 23) | (1u << 24) | (1u << 25);
    //setting pins as output
    sio_hw -> gpio_oe_set = (1u << 22) | (1u << 23) | (1u << 24) | (1u << 25);
}

void init_inputs() {
    // fill in
    //gpio_init(21);
}

void init_keypad() {
    // fill in
}

int main() {
    // Configures our microcontroller to 
    // communicate over UART through the TX/RX pins
    stdio_init_all();

    // Leave commented until you actually need it.
    // Can take significantly longer to upload code when uncommented.
    autotest();
    
    ////////////////////////////////////
    // All your code goes below.
    
    init_outputs();
    
    //for(;;){
        //setting each pin to HIGH
        sio_hw -> gpio_set = 1u << 22;
        sleep_ms(500);
        sio_hw -> gpio_set = 1u << 23;
        sleep_ms(500);
        sio_hw -> gpio_set = 1u << 24;
        sleep_ms(500);
        sio_hw -> gpio_set = 1u << 25;
        sleep_ms(500);

        //clearing each pin
        sio_hw -> gpio_clr = 1u << 22;
        sleep_ms(500);
        sio_hw -> gpio_clr = 1u << 23;
        sleep_ms(500);
        sio_hw -> gpio_clr = 1u << 24;
        sleep_ms(500);
        sio_hw -> gpio_clr = 1u << 25;
        sleep_ms(500);
    //}

    // Never reached.
    return 0;

    // All your code goes above.
    ////////////////////////////////////
}