MCU     = pic18f4550
CC      = sdcc
CFLAGS  = -mpic18 -p$(MCU) --use-non-free -I../../core

SRCS    = ktos_bsp.c \
          ../../core/ktos.c \
          ../../core/ktos_common.c

TARGET  = ktos_$(MCU)

# SDCC uses .rel object files
RELS    = $(SRCS:.c=.rel)

all: $(TARGET).hex

$(TARGET).hex: $(RELS)
	$(CC) $(CFLAGS) -o $(TARGET).hex $^

%.rel: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(RELS) $(TARGET).hex $(TARGET).map $(TARGET).lst
