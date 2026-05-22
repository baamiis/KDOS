TARGET  = ktos_bsp_stm32f411ce
MCU     = cortex-m4
FPU     = -mfpu=fpv4-sp-d16 -mfloat-abi=hard

CC      = arm-none-eabi-gcc
AR      = arm-none-eabi-ar

INCDIRS = -I../../include

CFLAGS  = -mcpu=$(MCU) -mthumb $(FPU) -O2 -Wall -ffunction-sections \
           -fdata-sections $(INCDIRS) -DSTM32F411xE

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
