MCU      = msp430g2553
CC       = msp430-elf-gcc
OBJCOPY  = msp430-elf-objcopy

CFLAGS   = -mmcu=$(MCU) -Wall -Wextra -Os \
           -I../../../core -I../../../bsp/msp430g2553

ROOT     = ../../..
CORE     = $(ROOT)/core
BSP      = $(ROOT)/bsp/msp430g2553

SRCS     = main.c \
           $(CORE)/ktos.c \
           $(BSP)/ktos_bsp.c

ASM_SRCS = $(BSP)/ktos_bsp_asm.S

OBJS     = $(notdir $(SRCS:.c=.o)) $(notdir $(ASM_SRCS:.S=.o))
TARGET   = hello_ktos

.PHONY: all flash clean

all: $(TARGET).elf

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^
	@echo "  Build OK: $(TARGET).elf"

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

ktos.o: $(CORE)/ktos.c
	$(CC) $(CFLAGS) -c -o $@ $<

ktos_bsp.o: $(BSP)/ktos_bsp.c
	$(CC) $(CFLAGS) -c -o $@ $<

ktos_bsp_asm.o: $(BSP)/ktos_bsp_asm.S
	$(CC) $(CFLAGS) -c -o $@ $<

flash: $(TARGET).elf
	mspdebug rf2500 "prog $(TARGET).elf"

clean:
	rm -f $(OBJS) $(TARGET).elf
