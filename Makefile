SRC_DIR := src
BUILD_DIR := bin
PRELOADER_DIR := preloader

OBJS := $(SRC_DIR)/main.o \
		$(SRC_DIR)/preloader.o \
		$(SRC_DIR)/ihex.o \
		$(SRC_DIR)/menu.o
LIBS := 

CC := $(PREFIX)gcc
LD := $(PREFIX)gcc

CFLAGS := -O2 -g
LDFLAGS := $(LIBS)

PREFIX := /usr/local

all: sflasher

sflasher: $(OBJS)
	mkdir -p $(BUILD_DIR)
	$(LD) -o $(BUILD_DIR)/$@ $^ $(LDFLAGS)

%.o: %.c %.h
	$(CC) -c -o $@ $< $(CFLAGS)

$(SRC_DIR)/preloader.o: $(SRC_DIR)/preloader.c
	make -C $(PRELOADER_DIR)
	$(CC) -c -o $@ $< $(CFLAGS)

install:
	install -Dm755 $(BUILD_DIR)/sflasher $(PREFIX)/bin

clean:
	rm -f $(OBJS)
	make -C $(PRELOADER_DIR) clean
