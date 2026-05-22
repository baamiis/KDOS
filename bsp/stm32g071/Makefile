CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy

MCU     = cortex-m0plus
TARGET  = ktos_stm32g071

SRCS    = ktos_bsp.c \
          ../../core/ktos.c \
          ../../core/ktos_common.c

CFLAGS  = -mcpu=$(MCU) -mthumb -O2 -Wall \
          -I../../core \
          -ffreestanding -nostdlib

LDFLAGS = -mcpu=$(MCU) -mthumb -nostdlib \
          -Wl,--gc-sections -T stm32g071.ld

OBJS    = $(SRCS:.c=.o)

all: $(TARGET).elf

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET).elf
