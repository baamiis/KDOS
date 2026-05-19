MCU     = cortex-m0plus
CC      = arm-none-eabi-gcc
CFLAGS  = -mcpu=$(MCU) -mthumb -Wall -Wextra -Os -I../../core \
          -ffunction-sections -fdata-sections

SRCS    = ktos_bsp.c \
          ../../core/ktos.c \
          ../../core/ktos_multi.c

OBJS    = $(SRCS:.c=.o)
TARGET  = ktos_samd21g18

all: $(TARGET).elf

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -Wl,--gc-sections --specs=nosys.specs

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET).elf
