cmake_minimum_required(VERSION 3.7)
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
        raspberrypi/ux.h
        raspberrypi/fsw.h
        raspberrypi/leds.h
        raspberrypi/midi.h
        raspberrypi/flash.c
        raspberrypi/lcd.c
        raspberrypi/main.c
        raspberrypi/midi.c
        raspberrypi/fsw-usb.c
        raspberrypi/ux-tty.c)

find_program(SCP_EXECUTABLE scp)
if(SCP_EXECUTABLE)
    add_custom_target(delivery
            COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
            COMMAND ${SCP_EXECUTABLE} ${CMAKE_PROJECT_NAME}
            pi:gopath/src/github.com/JamesDunne/eminor3/)
else()
    message(SEND_ERROR "Require scp for the delivery target")
endif()
