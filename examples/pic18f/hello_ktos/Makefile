MCU      = pic18f4550
CC       = sdcc

CFLAGS   = -mpic18 -p$(MCU) --use-non-free \
           -I../../../core -I../../../bsp/pic18f

ROOT     = ../../..
CORE     = $(ROOT)/core
BSP      = $(ROOT)/bsp/pic18f

SRCS     = main.c \
           $(CORE)/ktos.c \
           $(BSP)/ktos_bsp.c

RELS     = $(notdir $(SRCS:.c=.rel))
TARGET   = hello_ktos

.PHONY: all clean

all: $(TARGET).hex

$(TARGET).hex: $(RELS)
	$(CC) $(CFLAGS) -o $(TARGET).hex $^
	@echo "  Build OK: $(TARGET).hex"

main.rel: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

ktos.rel: $(CORE)/ktos.c
	$(CC) $(CFLAGS) -c -o $@ $<

ktos_bsp.rel: $(BSP)/ktos_bsp.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(RELS) $(TARGET).hex $(TARGET).map $(TARGET).lst
