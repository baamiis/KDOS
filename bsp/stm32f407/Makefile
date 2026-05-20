CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
AR       = $(CROSS)ar

CPU_FLAGS = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

CFLAGS  = $(CPU_FLAGS) -O2 -Wall -Wextra -ffunction-sections -fdata-sections
CFLAGS += -DSTM32F407xx
CFLAGS += -I../../include

SRC     = ktos_bsp.c
OBJ     = $(SRC:.c=.o)
LIB     = libktos_bsp_stm32f407.a

.PHONY: all clean

all: $(LIB)

$(LIB): $(OBJ)
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(LIB)
