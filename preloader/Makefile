SRC_DIR := src
LIB_DIR := lib
BUILD_DIR := bin

OBJS := $(SRC_DIR)/uart.o \
		$(SRC_DIR)/boot.o \
		$(SRC_DIR)/main.o \
		$(SRC_DIR)/ram.o \
		$(SRC_DIR)/flash.o

LIBS := $(LIB_DIR)/libgcc.a \
		$(LIB_DIR)/libc_nano.a

PREFIX := arm-none-eabi-

LDSCRIPT := $(SRC_DIR)/linker.ld

CC := $(PREFIX)gcc
CFLAGS := -Os -march=armv5tej

AS := $(PREFIX)as
ASFLAGS := -march=armv5tej

OBJCOPY := $(PREFIX)objcopy
OBJCOPYFLAGS := 

LD := $(PREFIX)ld
LDFLAGS := -T $(LDSCRIPT)

all: preloader.bin

preloader.bin: preloader
	$(OBJCOPY) -I elf32-littlearm -O binary -j .text -j .data $(BUILD_DIR)/$< $(BUILD_DIR)/$@ $(OBJCOPYFLAGS)

preloader: $(OBJS)
	mkdir -p $(BUILD_DIR)
	$(LD) -o $(BUILD_DIR)/$@ $^ --start-group $(LIBS) --end-group $(LDFLAGS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

%.o: %.s
	$(AS) -o $@ $< $(ASFLAGS)

clean:
	rm -f $(OBJS)
