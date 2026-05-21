MCU     = msp430g2553
CC      = msp430-elf-gcc
AS      = msp430-elf-gcc
CFLAGS  = -mmcu=$(MCU) -Wall -Wextra -Os -I../../core
ASFLAGS = -mmcu=$(MCU)

SRCS    = ktos_bsp.c \
          ../../core/ktos.c \
          ../../core/ktos_common.c

ASM_SRCS = ktos_bsp_asm.S

OBJS    = ktos_bsp.o ktos.o ktos_common.o $(ASM_SRCS:.S=.o)
TARGET  = ktos_$(MCU)

all: $(TARGET).elf

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

ktos_bsp.o: ktos_bsp.c
	$(CC) $(CFLAGS) -c -o $@ $<

ktos.o: ../../core/ktos.c
	$(CC) $(CFLAGS) -c -o $@ $<

ktos_common.o: ../../core/ktos_common.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.S
	$(AS) $(ASFLAGS) -c -o $@ $<

clean:
	rm -f ktos_bsp.o ktos.o ktos_common.o $(TARGET).elf
