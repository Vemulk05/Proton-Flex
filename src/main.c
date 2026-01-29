#include <stdio.h>
#include "pico/stdlib.h"
#include <stdbool.h>

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

    //defining pins 21-26
    io_bank0_hw -> io[21].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    io_bank0_hw -> io[26].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;

    //set output enable to 0 to define input for select pins
    sio_hw -> gpio_oe_clr = (1u << 21) | (1u << 26);
}

void init_keypad() {
    // fill in
    io_bank0_hw -> io[2].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    io_bank0_hw -> io[3].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    io_bank0_hw -> io[4].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    io_bank0_hw -> io[5].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;

    io_bank0_hw -> io[6].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    io_bank0_hw -> io[7].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    io_bank0_hw -> io[8].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;
    io_bank0_hw -> io[9].ctrl = GPIO_FUNC_SIO << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB;

    sio_hw -> gpio_oe_clr = (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5);
    sio_hw -> gpio_oe_set = (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9);
    sio_hw -> gpio_clr = (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9);
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
    
    /*for(;;){
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
    }*/

    init_inputs();
    
    /*for(;;){
        if (!(sio_hw -> gpio_in & (1u << 21))){
            sio_hw -> gpio_set = (1u << 22) | (1u << 23) | (1u << 24) | (1u << 25);
        }
        else if (!(sio_hw -> gpio_in & (1u << 26))){
            sio_hw -> gpio_clr = (1u << 22) | (1u << 23) | (1u << 24) | (1u << 25);
        }

        sleep_ms(10);
    }*/

    init_keypad();

    int COLS[] = {6, 7, 8, 9};  // COL4=GP6, COL3=GP7, COL2=GP8, COL1=GP9
    int ROWS[] = {2, 3, 4, 5};  // ROW4=GP2, ROW3=GP3, ROW2=GP4, ROW1=GP5
    int LEDS[] = {25, 24, 23, 22}; //LED array needed to iterate

    while(true){
        for(int i = 0; i < 4; i++){
            sio_hw -> gpio_set = 1u << COLS[i];
            sleep_ms(10);
            uint32_t pinval = (sio_hw -> gpio_in & (1u << ROWS[i]));
        
        if (pinval == 0){
            sio_hw -> gpio_clr = (1u << LEDS[i]);
        }
        else{
            sio_hw -> gpio_set = (1u << LEDS[i]);
        }
        sio_hw -> gpio_clr = (1u << COLS[i]);
    }
}

    // Never reached.
    return 0;

    // All your code goes above.
    ////////////////////////////////////
}