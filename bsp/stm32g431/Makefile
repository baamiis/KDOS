CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy

MCU     = cortex-m4
FPU     = -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS  = -mcpu=$(MCU) -mthumb $(FPU) -O2 -Wall \
           -ffreestanding -nostdlib \
           -I../../core

SRCS    = ktos_bsp.c ../../core/ktos.c ../../core/ktos_common.c
OBJS    = $(SRCS:.c=.o)
TARGET  = ktos_stm32g431

all: $(TARGET).elf

$(TARGET).elf: $(SRCS)
	$(CC) $(CFLAGS) -T stm32g431.ld -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET).elf $(TARGET).bin *.o

.PHONY: all clean
