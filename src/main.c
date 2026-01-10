#include <stdio.h>
#include "pico/stdlib.h"

//////////////////////////////////////////////////////////////////////////////

const char* username = "username";

//////////////////////////////////////////////////////////////////////////////

void autotest();

void init_outputs() {
    // fill in
}

void init_inputs() {
    // fill in
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
    // autotest();
    
    ////////////////////////////////////
    // All your code goes below.
    
    
    
    // All your code goes above.
    ////////////////////////////////////

    // An infinite loop is necessary to 
    // ensure control flow remains with user.
    for(;;);

    // Never reached.
    return 0;
}