#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/ioctl.h>

#include "types.h"
#include "hardware.h"

// Global variable for holding file descriptor to talk to MIDI communications:
int midi_fd = -1;

// Use "Fore" USB-MIDI adapter device on Raspberry Pi 3:
// P:  Vendor=552d ProdID=4348 Rev=02.11
// S:  Product=USB Midi
const char *midi_fname = "/dev/snd/midiC1D0";

// Open UART0 device for MIDI communications and set baud rate to 31250 per MIDI standard:
int midi_init(void) {
    midi_fd = open(midi_fname, O_WRONLY);
    if (midi_fd == -1) {
        char err[100];
        sprintf(err, "open('%s')", midi_fname);
        perror(err);
        return 1;
    }

    return 0;
}

void midi_send_cmd1_impl(u8 cmd_byte, u8 data1) {
    ssize_t count;
    u8 buf[2];
    buf[0] = cmd_byte;
    buf[1] = data1;
    count = write(midi_fd, buf, 2);
    if (count != 2) {
        perror("Error sending MIDI bytes");
        return;
    }
    printf("MIDI: %02X %02X\n", cmd_byte, data1);
}

void midi_send_cmd2_impl(u8 cmd_byte, u8 data1, u8 data2) {
    ssize_t count;
    u8 buf[3];
    buf[0] = cmd_byte;
    buf[1] = data1;
    buf[2] = data2;
    count = write(midi_fd, buf, 3);
    if (count != 3) {
        perror("Error sending MIDI bytes");
        return;
    }
    printf("MIDI: %02X %02X %02X\n", cmd_byte, data1, data2);
}

// 256 byte buffer for batching up SysEx data to send in one write() call:
u8 sysex[256];
size_t sysex_p = 0;

// Buffer up SysEx data until terminating F7 byte is encountered:
void midi_send_sysex(u8 byte) {
    //printf("MIDI: %02X\n", byte);

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

        printf("MIDI SysEx:");
        for (i = 0; i < write_count; i++) {
            printf(" %02X", sysex[i]);
        }
        printf("\n");

        ssize_t count = write(midi_fd, sysex, write_count);
        if (count < 0) {
            perror("write in midi_send_sysex");
            return;
        }
        if (count != write_count) {
            fprintf(stderr, "midi_send_sysex write didnt write enough bytes\n");
            return;
        }
    }
}
