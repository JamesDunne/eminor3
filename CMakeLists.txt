cmake_minimum_required(VERSION 3.7)
project(eminor3 C)

set(CMAKE_C_STANDARD 11)

include_directories(common)
include_directories(raspberrypi)

add_definitions()

add_executable(eminor3-pi
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
        raspberrypi/ux.h
        raspberrypi/fsw.h
        raspberrypi/leds.h
        raspberrypi/midi.h
        raspberrypi/flash.c
        raspberrypi/lcd.c
        raspberrypi/main.c
        raspberrypi/midi.c
        raspberrypi/fsw-usb.c
        raspberrypi/ts-input.c
        raspberrypi/ts-input.h
        raspberrypi/ux-tty.c)

target_compile_definitions(eminor3-pi PRIVATE -DHWFEAT_REPORT -DHWFEAT_TOUCHSCREEN)

find_program(SCP_EXECUTABLE scp)
if(SCP_EXECUTABLE)
    add_custom_target(delivery
            COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
            COMMAND ${SCP_EXECUTABLE} eminor3-pi
            pi:go/src/github.com/JamesDunne/eminor3/)
    add_dependencies(delivery eminor3-pi)
else()
    message(SEND_ERROR "Require scp for the delivery target")
endif()

add_executable(eminor3-darwin
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
        raspberrypi/ux.h
        raspberrypi/fsw.h
        raspberrypi/leds.h
        raspberrypi/midi.h
        raspberrypi/flash.c
        raspberrypi/lcd.c
        raspberrypi/main.c
        null/midi.c
        null/fsw.c
        null/ts-input.c
        raspberrypi/ts-input.h
        raspberrypi/ux-tty.c)

target_compile_definitions(eminor3-darwin PRIVATE -DHWFEAT_REPORT)
