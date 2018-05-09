#include <util/crc16.h>
#include "Arduino.h"
extern "C" {
#include <twi.h>
};

#include "attiny_flasher.h"

#define ELEMENTS(arr)  (sizeof(arr) / sizeof((arr)[0]))


void setup() {
    delay(5000);
    twi_init();
}


#define ENDTRANS_SUCCESS 0
#define ENDTRANS_DATA_TOO_LONG 1
#define ENDTRANS_ADDR_NACK 2
#define ENDTRANS_DATA_NACK 3
#define ENDTRANS_ERROR 4

#define debug_msg(...)   Serial.print(__VA_ARGS__)


// The LEFT ATTiny has a reset pin directly connected to the ATMega
void reset_left_attiny() {
    // Hold the left ATTiny in reset,
    DDRC |= _BV(6);
    PORTC &= ~_BV(6);
    delay(30);
    DDRC &= ~_BV(6); // Turn the ATTiny back on
}

// The RIGHT ATTiny is on the other side of the wired i2c bus.
// Our best chance at resetting it is by toggling the current limiter.

void reset_right_attiny() {
    // Hold the left ATTiny in reset,
    DDRC |= _BV(7);
    PORTC &= ~_BV(7);
    delay(1000);
    PORTC |= _BV(7); // Turn the ATTiny back on
}

void print_result(uint8_t result) {
    debug_msg(F("result = "));
    debug_msg(result);

}

int run_command(uint8_t address, byte command) {

// erase user space, dummy end uint8_t
    byte data[] = {command, 0x00};
    uint8_t result = twi_writeTo(address, data, ELEMENTS(data), true, true);

	
    print_result(result);
    debug_msg("\n");
    return result;
}



uint8_t read_crc16(byte addr, byte *version, uint16_t *crc16, uint16_t offset, uint16_t length) {
    uint8_t result = ENDTRANS_ADDR_NACK;


// get version and CRC16 // addr (lo) // addr (hi) // len (lo) // len (hi)
    uint8_t data[] = { (0x06), (uint8_t)(offset & 0xff), (uint8_t)(offset >> 8), (uint8_t)(length & 0xff), (uint8_t)(length >> 8) };
    result = twi_writeTo(addr, data, ELEMENTS(data), true, true);


    if (result != ENDTRANS_SUCCESS) {
        return result;
    }

    uint8_t rxBuffer[3];

    // perform blocking read into buffer
    uint8_t read = twi_readFrom(addr, rxBuffer, ELEMENTS(rxBuffer), true);
    if (read == ENDTRANS_SUCCESS) {
    }
    if (read < ENDTRANS_DATA_NACK) {
        return 0xFF;
    }
    uint8_t v = rxBuffer[0];
    *version = v;
    uint8_t crc16_lo = rxBuffer[1];
    uint8_t crc16_hi = rxBuffer[2];
    *crc16 = (crc16_hi << 8) | crc16_lo;
    return result;
}


void get_version (byte addr) {

    byte result = ENDTRANS_ADDR_NACK;
    while (result != ENDTRANS_SUCCESS) {
        debug_msg(F("\nReading CRC16: "));

        byte version;
        uint16_t crc16;
        result = read_crc16(addr, &version, &crc16, 0, firmware_length);

        print_result(result);
	
        if (result != ENDTRANS_SUCCESS) {
            _delay_ms(100);
            continue;
        }
        debug_msg(F("\nVersion: "));
        debug_msg(version);
        debug_msg(F("\nExisting CRC16 of 0000-1FFF: "));
        debug_msg(crc16, HEX);
    }

}



int erase_program(uint8_t addr) {
// erase user space
    uint8_t data[] = { 0x04 };
    uint8_t result = twi_writeTo(addr, data, ELEMENTS(data), true, true);

    debug_msg(F("\nErasing: "));
    print_result(result);
    if (result != ENDTRANS_SUCCESS) {
        _delay_ms(1000);
        debug_msg(F("failed.\n"));
        return -1;
    }
    return 0;

}


int write_firmware(uint8_t addr ) {

    uint8_t result = ENDTRANS_DATA_NACK;
    uint8_t o = 0;

    for (uint16_t i = 0; i < firmware_length; i += page_size) {
        debug_msg(F("\nPage "));
        debug_msg(offsets[o]);
        debug_msg(F(" setting address: "));

// write page addr
        uint8_t data[] = { 0x01, (uint8_t)(offsets[o] & 0xff), (uint8_t)(offsets[o] >> 8)};
        result = twi_writeTo(addr, data, ELEMENTS(data), true, true);
        print_result(result);

        _delay_ms(DELAY);
        // got something other than ACK. Start over.
        if (result != ENDTRANS_SUCCESS) {
            debug_msg(F("Error\n"));
            return -1;
        }

        // transmit each frame separately

        for (uint8_t frame = 0; frame < page_size / frame_size; frame++) {
            uint8_t data_counter =0;
            uint8_t data[frame_size +4] = {0};
            data[data_counter++] = 0x2; // continue page
            uint16_t crc16 = 0xffff;
            for (uint8_t j = frame * frame_size; j < (frame + 1) * frame_size; j++) {
                if (i + j < firmware_length) {
                    uint8_t b = pgm_read_byte(&firmware[i + j]);
                    data[data_counter++] = b;
                    crc16 = _crc16_update(crc16, b);
                } else {
                    data[data_counter++] = blank;
                    crc16 = _crc16_update(crc16, blank);
                }
            }
            // write the CRC16, little end first
            data[data_counter++] = (uint8_t)(crc16 & 0xff);
            data[data_counter++] = (uint8_t)(crc16 >> 8);
            data[data_counter++] = (0x00); // dummy end uint8_t

            result = twi_writeTo(addr, data, ELEMENTS(data), true, true);
            debug_msg(F("Frame "));
            debug_msg(frame);
	    debug_msg(F(": "));
            print_result(result);
            // got something other than NACK. Start over.
            if (result != ENDTRANS_DATA_NACK) {
                debug_msg(F("\nERROR: Got something other than NACK\n") );
                return -1;
            }
            delay(DELAY);
        }
        o++;
    }
    return 0;
}


int verify_firmware(byte addr) {
    byte result = ENDTRANS_DATA_NACK;
    // verify firmware
    debug_msg(F("Verifying install\n"));
    while (result != ENDTRANS_SUCCESS) {
        debug_msg(F("CRC16: "));

        byte version;
        uint16_t crc16;
        // skip the first 4 bytes, are they were probably overwritten by the reset vector preservation
        result = read_crc16(addr, &version, &crc16, offsets[0] + 4, firmware_length - 4);

        debug_msg(result);

        if (result != ENDTRANS_SUCCESS) {
            _delay_ms(100);
            continue;
        }
        debug_msg(F("\nVersion: "));
        debug_msg(version);
        debug_msg(F("\nCRC CRC16 of "));
        debug_msg(offsets[0] + 4, HEX);
        debug_msg(F("-"));
        debug_msg(offsets[0] + firmware_length, HEX);
        debug_msg(F(": "));
        debug_msg(crc16, HEX);

        // calculate our own CRC16
        uint16_t check_crc16 = 0xffff;
        for (uint16_t i = 4; i < firmware_length; i++) {
            check_crc16 = _crc16_update(check_crc16, pgm_read_byte(&firmware[i]));
        }
        if (crc16 != check_crc16) {
            debug_msg(F("does not match: "));
            debug_msg(check_crc16, HEX);
            return -1;
        }
        debug_msg(F(": OK\n"));
    }
    return 0;
}

byte update_attiny(byte addr) {
    debug_msg(F("Communicating\n"));

    get_version(addr);

    int erased = erase_program(addr);

    if (erased == -1) {

        return 0;
    }

    int firmware_written = write_firmware(addr);
    if(firmware_written == -1) {
        debug_msg(F("Failed.\n"));
        return 0;
    }

    int firmware_verified = verify_firmware(addr);
    if(firmware_verified == -1) {
        debug_msg(F("Failed.\n"));
        return 0;
    }

    debug_msg(F("Resetting: "));
    run_command(addr, 0x03); // execute app
    debug_msg(F("Done!\n"));

    return 1;
}

int left_written = 0;
int right_written = 0;

void loop() {
    delay(5000);

    if (left_written > 0) {
        debug_msg(F("Done with left side.\n"));
        // we're done
    } else {
    	debug_msg(F("Updating left side\n"));
        reset_left_attiny();
        left_written = update_attiny(LEFT_ADDRESS);

    }

    if (right_written > 0) {
        debug_msg(F("Done with right side.\n"));
        // we're done
    } else {
    	debug_msg(F("Updating right side\n"));
        reset_right_attiny();
        right_written = update_attiny(RIGHT_ADDRESS);
    }

    if (left_written && right_written  ) {
        debug_msg (F("Both ATTiny MCUs have been flashed\nIt is now safe to reload the regular firmware\n"));
        return;
    }


}

