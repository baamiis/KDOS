CC      = xtensa-lx106-elf-gcc
CFLAGS  = -mlongcalls -Wall -Wextra -Os -I../../core

SRCS    = ktos_bsp.c \
          ../../core/ktos.c \
          ../../core/ktos_common.c
ASRCS   = ktos_bsp_asm.S

OBJS    = ktos_bsp.o ktos.o ktos_common.o $(ASRCS:.S=.o)
TARGET  = ktos_esp8266

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
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f ktos_bsp.o ktos.o ktos_common.o $(TARGET).elf
