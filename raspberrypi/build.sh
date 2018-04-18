#!/bin/bash

# http://www.welzels.de/blog/en/arm-cross-compiling-with-mac-os-x/
#CC=/usr/local/linaro/arm-linux-gnueabihf-raspbian/bin/arm-linux-gnueabihf-gcc
CC=gcc

#SRCS=(main.c ../common/controller.c ../common/controller-data.c ../common/util.c flash.c sx1509-fsw-leds.c lcd.c midi.c)
SRCS=(main.c ../common/controller.c ../common/controller-data.c ../common/util.c flash.c fsw-usb.c lcd.c midi.c)

$CC -g -o rpi1b -DHW_VERSION=5 -I../common "${SRCS[@]}"
