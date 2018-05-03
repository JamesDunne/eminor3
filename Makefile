BASE=common/controller-data.c \
     common/controller.c \
     common/flash_v5_bank0.h \
     common/flash_v5_bank1.h \
     common/flash_v5_bank2.h \
     common/hardware.h \
     common/program-v5.h \
     common/types.h \
     common/util.c \
     common/util.h \
     common/v5_bcd_lookup.h \
     common/v5_fx_names.h \
     raspberrypi/ux.h \
     raspberrypi/fsw.h \
     raspberrypi/leds.h \
     raspberrypi/midi.h \
     raspberrypi/flash.c \
     raspberrypi/lcd.c \
     raspberrypi/main.c

PI3=$(BASE) raspberrypi/midi.c \
            raspberrypi/fsw-usb.c \
            raspberrypi/ux-tty.c
PI3_OBJS=$(filter-out %.h,$(patsubst %.c,build-pi/%.o,$(PI3)))
PI3_CC="/Volumes/xtools/armv8-rpi3-linux-gnueabihf/bin/armv8-rpi3-linux-gnueabihf-gcc"
PI3_CFLAGS=-Icommon -Iraspberrypi

DARWIN=$(BASE) null/midi.c
DARWIN_OBJS=$(filter-out %.h,$($(notdir $(DARWIN)):%.c=build-darwin/%.o))
DARWIN_CC=$(CC)
DARWIN_CFLAGS=-Icommon -Iraspberrypi

all: build-pi/eminor3

build-pi/eminor3: $(PI3_OBJS)
	echo $(PI3_OBJS)
	$(PI3_CC) $(PI3_OBJS) -o build-pi/eminor3

build-pi/%.o: %.c
	@mkdir -p $(@D)
	$(PI3_CC) $(PI3_CFLAGS) -c $< -o $@

build-darwin/eminor3: $(DARWIN_OBJS)
	$(CC) $(DARWIN_OBJS) -o build-darwin/eminor3

build-darwin/%.o: %.c
	@mkdir -p build-darwin
	$(DARWIN_CC) $(DARWIN_CFLAGS) -c $< -o $@
