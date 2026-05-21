MCU     = atmega328p
F_CPU   = 16000000UL
CC      = avr-gcc
CFLAGS  = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Wall -Wextra -Os -I../../core

SRCS    = ktos_bsp.c \
          ../../core/ktos.c \
          ../../core/ktos_common.c

OBJS    = ktos_bsp.o ktos.o ktos_common.o
TARGET  = ktos_$(MCU)

all: $(TARGET).elf

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

ktos_bsp.o: ktos_bsp.c
	$(CC) $(CFLAGS) -c -o $@ $<

ktos.o: ../../core/ktos.c
	$(CC) $(CFLAGS) -c -o $@ $<

ktos_common.o: ../../core/ktos_common.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f ktos_bsp.o ktos.o ktos_common.o $(TARGET).elf
