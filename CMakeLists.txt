cmake_minimum_required(VERSION 3.10)
project(eminor3 C)

set(CMAKE_C_STANDARD 11)

include_directories(common)
include_directories(raspberrypi)

add_executable(eminor3
        common/controller-data.c
        common/controller.c
        common/flash_v5_bank0.h
        common/flash_v5_bank1.h
        common/flash_v5_bank2.h
        common/hardware.h
        common/program-v5.h
        common/types.h
        common/util.c
        common/util.h
        common/v5_bcd_lookup.h
        common/v5_fx_names.h
        raspberrypi/flash.c
        raspberrypi/fsw.h
        raspberrypi/i2c.h
        raspberrypi/lcd.c
        raspberrypi/leds.h
        raspberrypi/main.c
        raspberrypi/midi.c
        raspberrypi/midi.h
        raspberrypi/fsw-usb.c)
