#include "wire-protocol.h"
#include "keyscanner.h"
#include "led-spiout.h"

static inline void setup(void) {
    led_init();
    keyscanner_init();
    twi_init();
}

int main(void) {
    setup();
    while(1) {
        keyscanner_main();
    }
    __builtin_unreachable();
}
