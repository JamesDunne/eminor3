#include <stdio.h>
#include <string.h>
#include <fcntl.h>

#include <unistd.h>
#include <sys/ioctl.h>

#include <linux/i2c-dev.h>
// Terrible portability hack between arm-linux-gnueabihf-gcc on Mac OS X and native gcc on raspbian.
# ifndef I2C_M_RD
#include <linux/i2c.h>
# endif

#include "types.h"
#include "hardware.h"

// SX1509 breakout board exposes ADD1 and ADD0 jumpers for configuring I2C address.
// https://learn.sparkfun.com/tutorials/sx1509-io-expander-breakout-hookup-guide#sx1509-breakout-board-overview
// ADD1 = 0, ADD0 = 0
#define i2c_sx1509_btn_addr 0x3E
// ADD1 = 1, ADD0 = 0
#define i2c_sx1509_led_addr 0x70

int i2c_init(void);
void i2c_close(void);

int i2c_write(u8 slave_addr, u8 reg, u8 data);
int i2c_read(u8 slave_addr, u8 reg, u8 *result);

typedef u8 byte;
#include "sx1509_registers.h"

// Global file descriptor used to talk to the I2C bus:
int i2c_fd = -1;
// Default RPi B device name for the I2C bus exposed on GPIO2,3 pins (GPIO2=SDA, GPIO3=SCL):
const char *i2c_fname = "/dev/i2c-1";

// Returns a new file descriptor for communicating with the I2C bus:
int i2c_init(void) {
    if ((i2c_fd = open(i2c_fname, O_RDWR)) < 0) {
        char err[200];
        sprintf(err, "open('%s') in i2c_init", i2c_fname);
        perror(err);
        return -1;
    }

    // NOTE we do not call ioctl with I2C_SLAVE here because we always use the I2C_RDWR ioctl operation to do
    // writes, reads, and combined write-reads. I2C_SLAVE would be used to set the I2C slave address to communicate
    // with. With I2C_RDWR operation, you specify the slave address every time. There is no need to use normal write()
    // or read() syscalls with an I2C device which does not support SMBUS protocol. I2C_RDWR is much better especially
    // for reading device registers which requires a write first before reading the response.

    return i2c_fd;
}

void i2c_close(void) {
    close(i2c_fd);
}

// Write to an I2C slave device's register:
int i2c_write(u8 slave_addr, u8 reg, u8 data) {
    int retval;
    u8 outbuf[2];

    struct i2c_msg msgs[1];
    struct i2c_rdwr_ioctl_data msgset[1];

    outbuf[0] = reg;
    outbuf[1] = data;

    msgs[0].addr = slave_addr;
    msgs[0].flags = 0;
    msgs[0].len = 2;
    msgs[0].buf = outbuf;

    msgset[0].msgs = msgs;
    msgset[0].nmsgs = 1;

    if (ioctl(i2c_fd, I2C_RDWR, &msgset) < 0) {
        perror("ioctl(I2C_RDWR) in i2c_write");
        return -1;
    }

    return 0;
}

// Read the given I2C slave device's register and return the read value in `*result`:
int i2c_read(u8 slave_addr, u8 reg, u8 *result) {
    int retval;
    u8 outbuf[1], inbuf[1];
    struct i2c_msg msgs[2];
    struct i2c_rdwr_ioctl_data msgset[1];

    msgs[0].addr = slave_addr;
    msgs[0].flags = 0;
    msgs[0].len = 1;
    msgs[0].buf = outbuf;

    msgs[1].addr = slave_addr;
    msgs[1].flags = I2C_M_RD | I2C_M_NOSTART;
    msgs[1].len = 1;
    msgs[1].buf = inbuf;

    msgset[0].msgs = msgs;
    msgset[0].nmsgs = 2;

    outbuf[0] = reg;

    inbuf[0] = 0;

    *result = 0;
    if (ioctl(i2c_fd, I2C_RDWR, &msgset) < 0) {
        perror("ioctl(I2C_RDWR) in i2c_read");
        return -1;
    }

    *result = inbuf[0];
    return 0;
}

// Initialize SX1509 for buttons by enabling all pins as inputs and pull-up resistors:
int fsw_init(void) {
    if (i2c_fd == -1) {
        // Open I2C bus for FSWs and LEDs:
        if (i2c_init() < 0) {
            return 2;
        }
    }

    // Set all pins as inputs:
    if (i2c_write(i2c_sx1509_btn_addr, REG_DIR_A,           0xFF) != 0) goto fail;
    if (i2c_write(i2c_sx1509_btn_addr, REG_DIR_B,           0xFF) != 0) goto fail;
    if (i2c_write(i2c_sx1509_btn_addr, REG_INPUT_DISABLE_A, 0x00) != 0) goto fail;
    if (i2c_write(i2c_sx1509_btn_addr, REG_INPUT_DISABLE_B, 0x00) != 0) goto fail;

    // Pull-up resistor on all pins for active-low buttons:
    if (i2c_write(i2c_sx1509_btn_addr, REG_PULL_UP_A,   0xFF) != 0) goto fail;
    if (i2c_write(i2c_sx1509_btn_addr, REG_PULL_UP_B,   0xFF) != 0) goto fail;
    if (i2c_write(i2c_sx1509_btn_addr, REG_PULL_DOWN_A, 0x00) != 0) goto fail;
    if (i2c_write(i2c_sx1509_btn_addr, REG_PULL_DOWN_B, 0x00) != 0) goto fail;

    return 0;

    fail:
    return 2;
}

// Initialize SX1509 for LEDs by configuring all pins as outputs:
int led_init(void) {
    if (i2c_fd == -1) {
        // Open I2C bus for FSWs and LEDs:
        if (i2c_init() < 0) {
            return 3;
        }
    }

    // Set all pins as outputs for LEDs:
    if (i2c_write(i2c_sx1509_led_addr, REG_DIR_A,           0x00) != 0) goto fail;
    if (i2c_write(i2c_sx1509_led_addr, REG_DIR_B,           0x00) != 0) goto fail;
    if (i2c_write(i2c_sx1509_led_addr, REG_INPUT_DISABLE_A, 0xFF) != 0) goto fail;
    if (i2c_write(i2c_sx1509_led_addr, REG_INPUT_DISABLE_B, 0xFF) != 0) goto fail;

    // No pull-up or pull-down resistors on LED pins:
    if (i2c_write(i2c_sx1509_led_addr, REG_PULL_UP_A,   0x00) != 0) goto fail;
    if (i2c_write(i2c_sx1509_led_addr, REG_PULL_UP_B,   0x00) != 0) goto fail;
    if (i2c_write(i2c_sx1509_led_addr, REG_PULL_DOWN_A, 0x00) != 0) goto fail;
    if (i2c_write(i2c_sx1509_led_addr, REG_PULL_DOWN_B, 0x00) != 0) goto fail;

    return 0;

    fail:
    return 3;
}

// Poll 16 foot-switch states:
u16 fsw_poll(void) {
    u8 buf[2];
    // Read both data registers to get entire 16 bits of input state:
    if (i2c_read(i2c_sx1509_btn_addr, REG_DATA_A, &buf[0]) != 0) return 0;
    if (i2c_read(i2c_sx1509_btn_addr, REG_DATA_B, &buf[1]) != 0) return 0;
    // NOTE: we invert (~) button state because they are active low (0) and default high (1):
    return ~(u16)( ((u16)buf[1] << 8) | (u16)buf[0] );
}

// Set 16 LED states:
void led_set(u16 leds) {
    u8 buf[2];
    buf[0] = leds & (u8)0xFF;
    buf[1] = (u8)(leds >> (u8)8) & (u8)0xFF;
    // Read both data registers to get entire 16 bits of input state:
    if (i2c_write(i2c_sx1509_led_addr, REG_DATA_A, buf[0]) != 0) return;
    if (i2c_write(i2c_sx1509_led_addr, REG_DATA_B, buf[1]) != 0) return;
    return;
}
