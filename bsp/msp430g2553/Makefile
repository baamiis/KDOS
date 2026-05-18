MCU     = msp430g2553
CC      = msp430-elf-gcc
AS      = msp430-elf-gcc
CFLAGS  = -mmcu=$(MCU) -Wall -Wextra -Os -I../../core
ASFLAGS = -mmcu=$(MCU)

SRCS    = ktos_bsp.c \
          ../../core/ktos.c \
          ../../core/ktos_multi.c

ASM_SRCS = ktos_bsp_asm.S

OBJS    = $(SRCS:.c=.o) $(ASM_SRCS:.S=.o)
TARGET  = ktos_$(MCU)

all: $(TARGET).elf

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.S
	$(AS) $(ASFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET).elf
