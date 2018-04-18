#!/bin/bash

# http://www.welzels.de/blog/en/arm-cross-compiling-with-mac-os-x/
#CC=/usr/local/linaro/arm-linux-gnueabihf-raspbian/bin/arm-linux-gnueabihf-gcc
CC=gcc

SRCS=(main.c ../common/controller-v5.c flash.c i2c.c fsw.c lcd.c leds.c midi.c)

$CC -g -o rpi1b -DHW_VERSION=5 -I../common "${SRCS[@]}"
