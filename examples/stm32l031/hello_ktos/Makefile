MCU      = stm32l031
CC       = arm-none-eabi-gcc
OBJCOPY  = arm-none-eabi-objcopy

CFLAGS   = -mcpu=cortex-m0plus -mthumb -Wall -Wextra -Os \
           -ffunction-sections -fdata-sections \
           -I../../../core -I../../../bsp/stm32l031

ROOT     = ../../..
CORE     = $(ROOT)/core
BSP      = $(ROOT)/bsp/stm32l031

SRCS     = main.c \
           $(CORE)/ktos.c \
           $(BSP)/ktos_bsp.c

OBJS     = $(notdir $(SRCS:.c=.o))
TARGET   = hello_ktos
LD_SCRIPT = stm32l031.ld

LDFLAGS  = -Wl,--gc-sections --specs=nosys.specs --specs=nano.specs \
           -T$(LD_SCRIPT)

.PHONY: all flash clean

all: $(TARGET).elf

$(TARGET).elf: $(OBJS) $(LD_SCRIPT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)
	@echo "  Build OK: $(TARGET).elf"

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

ktos.o: $(CORE)/ktos.c
	$(CC) $(CFLAGS) -c -o $@ $<

ktos_bsp.o: $(BSP)/ktos_bsp.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET).elf
