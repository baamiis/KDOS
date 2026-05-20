TARGET  = ktos_bsp_nrf52840
MCU     = cortex-m4
FPU     = fpv4-sp-d16
FPABI   = hard

CROSS   = arm-none-eabi-
CC      = $(CROSS)gcc
AR      = $(CROSS)ar
OBJCOPY = $(CROSS)objcopy

KTOS_ROOT ?= ../..

CFLAGS  = -mcpu=$(MCU) -mthumb -mfpu=$(FPU) -mfloat-abi=$(FPABI)
CFLAGS += -O2 -Wall -Wextra -ffunction-sections -fdata-sections
CFLAGS += -I$(KTOS_ROOT)/include

SRCS    = ktos_bsp.c
OBJS    = $(SRCS:.c=.o)

all: lib$(TARGET).a

lib$(TARGET).a: $(OBJS)
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) lib$(TARGET).a

.PHONY: all clean
