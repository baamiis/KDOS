CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy

MCU     = cortex-m4
FPU     = -mfpu=fpv4-sp-d16 -mfloat-abi=hard
TARGET  = ktos_stm32g431

CFLAGS  = -mcpu=$(MCU) -mthumb $(FPU) -O2 -Wall \
           -I../../core -DSTM32G431xx

SRCS    = ktos_bsp.c ../../core/ktos.c ../../core/ktos_common.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET).elf

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) -nostartfiles -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET).elf
