#pragma once
#include <stdio.h>
#include <stdint.h>
#include "keyscanner.h"

#define TURNING_ON_CHATTER_WINDOW 2
#define LOCKED_ON_PERIOD 11
#define KEY_ON_CHATTER_WINDOW  5
#define TURNING_OFF_CHATTER_WINDOW 3
#define LOCKED_OFF_PERIOD 34

# define BAD_SWITCH_TURNING_ON_CHATTER_WINDOW 2
# define BAD_SWITCH_LOCKED_ON_PERIOD 22
# define BAD_SWITCH_KEY_ON_CHATTER_WINDOW  10
# define BAD_SWITCH_TURNING_OFF_CHATTER_WINDOW 35
# define BAD_SWITCH_LOCKED_OFF_PERIOD 50


#define CHATTER_DETECTED 1
#define NO_CHATTER_DETECTED 0

#define debug(x)  //printf(x); printf("\n");


enum { OFF, TURNING_ON, LOCKED_ON, ON, TURNING_OFF, LOCKED_OFF};


// these timers are in the same order as the enum above;
//
uint8_t regular_timers[] =           { 0,  2, 11,   5,   3,   34 };
uint8_t chattering_switch_timers[] = { 0,  2, 35,  10,  38,   50 };

typedef struct {
    uint8_t key_states[8];
    uint8_t cycles[8];
    uint8_t per_state_data[8];
    uint8_t key_chatters[8];
    uint8_t state;  // debounced state
} debounce_t;



void transition_to_state(debounce_t *debouncer, uint8_t i, uint8_t new_state, uint8_t chatter_detected) {
    debouncer->key_states[i]= new_state;
    debouncer->cycles[i]=0;
    if (chatter_detected) {
        debouncer->key_chatters[i]=1;
    }
    debouncer->per_state_data[i]=(debouncer->key_chatters[i] ? chattering_switch_timers[new_state]: regular_timers[new_state] );
}

void handle_state_off (uint8_t is_on, uint8_t i, debounce_t *debouncer, uint8_t *changes) {
    // if we get a single input sample that's "1", transition to "TURNING_ON".
    if (is_on) {
        transition_to_state(debouncer, i, TURNING_ON, NO_CHATTER_DETECTED );
    }
}

void handle_state_turning_on(uint8_t is_on, uint8_t i, debounce_t *debouncer, uint8_t *changes) {
    if (!is_on)  {
        transition_to_state(debouncer,i, OFF, CHATTER_DETECTED);
        return;
    }

    debouncer->per_state_data[i]--;

    if(debouncer->per_state_data[i]>0) {
        return;
    }

    *changes |= _BV(i);

    transition_to_state(debouncer,i, LOCKED_ON, NO_CHATTER_DETECTED);
}

void handle_state_locked_on(uint8_t is_on, uint8_t i, debounce_t *debouncer, uint8_t *changes) {
    // 	do not act on any input while the key is locked on
    if(debouncer->cycles[i] >= debouncer->per_state_data[i]) {
        transition_to_state(debouncer,i, ON, NO_CHATTER_DETECTED);
        return;
    }

    if (!is_on && ! debouncer->key_chatters[i] ) {
        transition_to_state(debouncer,i, LOCKED_ON, CHATTER_DETECTED);
    }
}

void handle_state_on(uint8_t is_on, uint8_t i, debounce_t *debouncer, uint8_t *changes) {
    if (is_on) {
        transition_to_state(debouncer,i, ON, NO_CHATTER_DETECTED);
        return;
    }

    // 	if all of the last 10ms of samples are "0", transition to "TURNING_OFF"
    debouncer->per_state_data[i]--;
    if ( debouncer->per_state_data[i] == 0) {
        transition_to_state(debouncer, i, TURNING_OFF, NO_CHATTER_DETECTED);
    }
}


void handle_state_turning_off(uint8_t is_on, uint8_t i, debounce_t *debouncer, uint8_t *changes) {
    if(is_on) {
        transition_to_state(debouncer, i, ON, CHATTER_DETECTED);
    }


    debouncer->per_state_data[i]--;
    if(debouncer->per_state_data[i]==0) {
        transition_to_state(debouncer, i, LOCKED_OFF, NO_CHATTER_DETECTED);
        *changes |= _BV(i);

    }
}
void handle_state_locked_off(uint8_t is_on, uint8_t i, debounce_t *debouncer, uint8_t *changes) {
    // 	after 45ms transition to "OFF"
    if(debouncer->cycles[i] >= debouncer->per_state_data[i] ) {
        transition_to_state(debouncer, i, OFF, NO_CHATTER_DETECTED);
        return;
    }
    // 	do not act on any input during the locked off window
    if (is_on) {
        transition_to_state(debouncer, i, LOCKED_OFF, CHATTER_DETECTED);
    }
}



static uint8_t debounce(uint8_t sample, debounce_t *debouncer) {
    uint8_t changes = 0;
    // Scan each pin from the bank
    for(int8_t i=0; i< COUNT_INPUT; i++) {

        uint8_t is_on=  !! (sample & _BV(i)) ;
        debouncer->cycles[i]++;

        switch (debouncer->key_states[i] ) {
        case OFF:
            handle_state_off(is_on, i, debouncer, &changes);
            break;

        case TURNING_ON:
            handle_state_turning_on(is_on, i, debouncer, &changes);
            break;

        case LOCKED_ON:
            handle_state_locked_on(is_on, i, debouncer, &changes);
            break;

        case ON:
            handle_state_on(is_on, i, debouncer, &changes);
            break;

        case TURNING_OFF:
            handle_state_turning_off(is_on, i, debouncer, &changes);
            break;

        case LOCKED_OFF:
            handle_state_locked_off(is_on, i, debouncer, &changes);
            break;
        };

    }


    debouncer->state ^= changes;

    return changes;
}
