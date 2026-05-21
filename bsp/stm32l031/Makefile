MCU     = stm32l031
CC      = arm-none-eabi-gcc
CFLAGS  = -mcpu=cortex-m0plus -mthumb -Wall -Wextra -Os -I../../core \
          -ffunction-sections -fdata-sections

SRCS    = ktos_bsp.c \
          ../../core/ktos.c \
          ../../core/ktos_common.c

OBJS    = ktos_bsp.o ktos.o ktos_common.o
TARGET  = ktos_$(MCU)

all: $(TARGET).elf

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -Wl,--gc-sections --specs=nosys.specs

ktos_bsp.o: ktos_bsp.c
	$(CC) $(CFLAGS) -c -o $@ $<

ktos.o: ../../core/ktos.c
	$(CC) $(CFLAGS) -c -o $@ $<

ktos_common.o: ../../core/ktos_common.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f ktos_bsp.o ktos.o ktos_common.o $(TARGET).elf
