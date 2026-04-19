MCU     = cortex-m3
CC      = arm-none-eabi-gcc
CFLAGS  = -mcpu=$(MCU) -mthumb -Wall -Wextra -Os -I../../core \
          -ffunction-sections -fdata-sections

SRCS    = ktos_bsp.c \
          ../../core/ktos.c \
          ../../core/ktos_multi.c

OBJS    = $(SRCS:.c=.o)
TARGET  = ktos_stm32f103

all: $(TARGET).elf

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -Wl,--gc-sections

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET).elf
