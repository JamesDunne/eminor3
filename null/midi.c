#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "types.h"
#include "hardware.h"

// Open UART0 device for MIDI communications and set baud rate to 31250 per MIDI standard:
int midi_init(void) {
    return 0;
}

void midi_send_cmd1_impl(u8 cmd_byte, u8 data1) {
    u8 buf[2];
    buf[0] = cmd_byte;
    buf[1] = data1;
    fprintf(stderr, "MIDI: %02X %02X\n", cmd_byte, data1);
}

void midi_send_cmd2_impl(u8 cmd_byte, u8 data1, u8 data2) {
    u8 buf[3];
    buf[0] = cmd_byte;
    buf[1] = data1;
    buf[2] = data2;
    fprintf(stderr, "MIDI: %02X %02X %02X\n", cmd_byte, data1, data2);
}

// 256 byte buffer for batching up SysEx data to send in one write() call:
u8 sysex[256];
size_t sysex_p = 0;

// Buffer up SysEx data until terminating F7 byte is encountered:
void midi_send_sysex(u8 byte) {
    //fprintf(stderr, "MIDI: %02X\n", byte);

    if (sysex_p >= 256) {
        fprintf(stderr, "MIDI SysEx data too large (>= 256 bytes)\n");
        return;
    }

    // Buffer data:
    sysex[sysex_p++] = byte;

    if (byte == 0xF7) {
        size_t i;
        size_t write_count = sysex_p;
        sysex_p = 0;

        fprintf(stderr, "MIDI SysEx:");
        for (i = 0; i < write_count; i++) {
            fprintf(stderr, " %02X", sysex[i]);
        }
        fprintf(stderr, "\n");
    }
}
