CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

MCU     = cortex-m4
FPU     = -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS  = -mcpu=$(MCU) -mthumb $(FPU) -O2 -Wall -Wextra \
           -ffreestanding -nostdlib \
           -I../../core

SRCS    = ktos_bsp.c ../../core/ktos.c ../../core/ktos_common.c
TARGET  = ktos_stm32g431

.PHONY: all clean

all: $(TARGET).elf

$(TARGET).elf: $(SRCS)
	$(CC) $(CFLAGS) -Wl,--gc-sections -o $@ $^
	$(SIZE) $@

clean:
	rm -f $(TARGET).elf $(TARGET).bin
