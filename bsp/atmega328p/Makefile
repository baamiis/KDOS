MCU     = atmega328p
F_CPU   = 16000000UL
CC      = avr-gcc
CFLAGS  = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Wall -Wextra -Os -I../../core

SRCS    = ktos_bsp.c \
          ../../core/ktos.c \
          ../../core/ktos_multi.c

OBJS    = $(SRCS:.c=.o)
TARGET  = ktos_$(MCU)

all: $(TARGET).elf

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET).elf
