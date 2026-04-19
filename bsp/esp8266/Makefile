CC      = xtensa-lx106-elf-gcc
CFLAGS  = -mlongcalls -Wall -Wextra -Os -I../../core

SRCS    = ktos_bsp.c \
          ../../core/ktos.c \
          ../../core/ktos_multi.c

OBJS    = $(SRCS:.c=.o)
TARGET  = ktos_esp8266

all: $(TARGET).elf

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET).elf
