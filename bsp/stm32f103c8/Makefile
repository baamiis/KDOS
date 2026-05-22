CC      = arm-none-eabi-gcc
AR      = arm-none-eabi-ar
CFLAGS  = -mcpu=cortex-m3 -mthumb -O2 -Wall -ffunction-sections -fdata-sections
CFLAGS += -I../../include
SRC     = ktos_bsp.c
OBJ     = $(SRC:.c=.o)
LIB     = libktos_bsp.a

all: $(LIB)

$(LIB): $(OBJ)
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(LIB)

.PHONY: all clean
