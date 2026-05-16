MCU      = atmega2560
F_CPU    = 16000000UL
CC       = avr-gcc
OBJCOPY  = avr-objcopy
AVRDUDE  = avrdude

CFLAGS   = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Wall -Wextra -Os \
           -I../../../core -I../../../bsp/atmega2560

ROOT     = ../../..
CORE     = $(ROOT)/core
BSP      = $(ROOT)/bsp/atmega2560

SRCS     = main.c \
           $(CORE)/ktos.c \
           $(BSP)/ktos_bsp.c

OBJS     = $(notdir $(SRCS:.c=.o))
TARGET   = hello_ktos

.PHONY: all flash clean

all: $(TARGET).hex

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@
	@echo "  Build OK: $(TARGET).hex"

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

ktos.o: $(CORE)/ktos.c
	$(CC) $(CFLAGS) -c -o $@ $<

ktos_bsp.o: $(BSP)/ktos_bsp.c
	$(CC) $(CFLAGS) -c -o $@ $<

flash: $(TARGET).hex
	@if [ -z "$(PORT)" ]; then echo "Usage: make flash PORT=/dev/ttyACM0"; exit 1; fi
	$(AVRDUDE) -p $(MCU) -c wiring -P $(PORT) -b 115200 -D -U flash:w:$(TARGET).hex:i

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).hex
