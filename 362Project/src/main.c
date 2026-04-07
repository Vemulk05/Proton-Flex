#include <stdio.h>
#include "pico/stdlib.h"
#include <stdbool.h>

//////////////////////////////////////////////////////////////////////////////

const char* username = "vemula2";

//////////////////////////////////////////////////////////////////////////////

void autotest();

void init_outputs() {
    // fill in

    //this is just setup/initialization
    gpio_init(22);
    gpio_init(23);
    gpio_init(24);
    gpio_init(25);

    //set the output direcition
    gpio_set_dir(22, true);
    gpio_set_dir(23, true);
    gpio_set_dir(24, true);
    gpio_set_dir(25, true);
    
}

void init_inputs() {
    // fill in

    gpio_init(21);
    gpio_init(26);

    //configuring 21 and 26 as inputs
    gpio_set_dir(21, false);
    gpio_set_dir(26, false);

}

void init_keypad() {
    // fill in

    gpio_init(2);
    gpio_init(3);
    gpio_init(4);
    gpio_init(5);
    gpio_init(6);
    gpio_init(7);
    gpio_init(8);
    gpio_init(9);

    for(int i = 2; i < 6; i++){
        gpio_set_dir(i, false);
    }

    for(int i = 6; i < 10; i++){
        gpio_set_dir(i, true);
    }
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
/*
    for(;;){
    

    //turn on each LED gp22-25 in sequence with 500 ms delay in between each led

    for(int i = 22; i < 26; i++){
        gpio_put(i, true);
        sleep_ms(500);
    }

    for(int i = 22; i < 26; i++){
        gpio_put(i, false);
        sleep_ms(500);
    }

    }
*/

    init_inputs();

/*
    for(;;){
        if(gpio_get(21) == true){
            for(int i = 22; i < 26; i++){
                gpio_put(i, true);
            }
        }
            else if(gpio_get(26) == true){
                for(int i = 22; i < 26; i++){
                    gpio_put(i, false);
                }
            }
        sleep_ms(10);
        }
*/



init_keypad();

/*
int COLS[] = {6, 7, 8, 9};  // COL4=GP6, COL3=GP7, COL2=GP8, COL1=GP9
int ROWS[] = {2, 3, 4, 5};  // ROW4=GP2, ROW3=GP3, ROW2=GP4, ROW1=GP5

while(true) {
    for(int i = 0; i < 4; i++){
        gpio_put(COLS[i], 1);
        sleep_ms(10);
        int read_val = gpio_get(ROWS[i]);
        if(read_val == 0){
            gpio_put(25 - i, false);
        }
        else{
            gpio_put(25 - i, true);
        }
        gpio_put(COLS[i], false);
    }
}

    // Never reached.
    return 0;
*/
}

    // All your code goes above.
    ////////////////////////////////////