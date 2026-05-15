# =============================================================================
# KTOS ESP8266 Hello example — Makefile
#
# Requires: xtensa-lx106-elf toolchain (~/.local/bin or ~/esp/xtensa-lx106-elf/bin),
#           ESP8266 Non-OS SDK (~/esp/sdk), esptool.py
# Usage:
#   make              — build firmware binaries
#   make flash PORT=/dev/ttyUSB0
#   make clean
# =============================================================================

# ---------- Toolchain --------------------------------------------------------
CC       := xtensa-lx106-elf-gcc
ESPTOOL  := esptool.py

# ---------- SDK path (override on command line if needed) --------------------
SDK_DIR  ?= $(HOME)/esp/sdk

# ---------- Project paths ----------------------------------------------------
ROOT      := ../../..
CORE      := $(ROOT)/core
BSP       := $(ROOT)/bsp/esp8266

# ---------- Compiler flags ---------------------------------------------------
INC       := -I$(CORE) -I$(SDK_DIR)/include
BASE      := -mlongcalls -Os -Wall -Wextra -ffunction-sections -fdata-sections

# All KTOS and application code — CALL0 ABI (no register windowing)
# Note: esp-2020r3 toolchain defaults to CALL0 ABI; -mabi=call0 not needed
C0FLAGS   := $(BASE) $(INC)

# startup.c only — windowed ABI so the ROM/SDK can call user_init() normally
WAFLAGS   := $(BASE) $(INC)

# ---------- Linker flags -----------------------------------------------------
LD_SCRIPT := ktos_esp8266.ld
LDFLAGS   := -nostdlib \
             -L$(SDK_DIR)/lib            \
             -Wl,-L$(SDK_DIR)/ld         \
             -Wl,--start-group           \
               -lgcc                     \
             -Wl,--end-group             \
             -T$(LD_SCRIPT)

# ---------- Sources ----------------------------------------------------------
# CALL0 — everything except startup.c
SRCS_C0 := main.c         \
            alloc.c        \
            $(CORE)/ktos.c \
            $(BSP)/ktos_bsp.c

# Assembly (CALL0, no ABI flags needed)
SRCS_AS := $(BSP)/ktos_bsp_asm.S

# Windowed ABI
SRCS_WA := startup.c

# Flatten object names into the local directory
OBJS_C0 := $(notdir $(SRCS_C0:.c=.o))
OBJS_AS := $(notdir $(SRCS_AS:.S=.o))
OBJS_WA := $(notdir $(SRCS_WA:.c=.o))

TARGET   := hello_ktos

# ---------- Targets ----------------------------------------------------------
.PHONY: all flash clean

all: $(TARGET).bin

# Link
$(TARGET).elf: $(OBJS_C0) $(OBJS_AS) $(OBJS_WA)
	$(CC) $(C0FLAGS) $(LDFLAGS) -o $@ $^

# Create flashable binary (all code in IRAM → only 0x00000.bin is produced)
$(TARGET).bin: $(TARGET).elf
	$(ESPTOOL) elf2image --output $(TARGET)- $<
	@echo ""
	@echo "  Build OK:"
	@echo "    $(TARGET)-0x00000.bin  (flash at 0x00000)"
	@echo ""

# Compile CALL0 sources (vpath for files in subdirectories)
ktos.o: $(CORE)/ktos.c
	$(CC) $(C0FLAGS) -c -o $@ $<

ktos_bsp.o: $(BSP)/ktos_bsp.c
	$(CC) $(C0FLAGS) -c -o $@ $<

main.o: main.c
	$(CC) $(C0FLAGS) -c -o $@ $<

alloc.o: alloc.c
	$(CC) $(C0FLAGS) -c -o $@ $<

# Compile assembly
ktos_bsp_asm.o: $(BSP)/ktos_bsp_asm.S
	$(CC) $(BASE) $(INC) -c -o $@ $<

# Compile windowed-ABI source
startup.o: startup.c
	$(CC) $(WAFLAGS) -c -o $@ $<

# Flash
flash: $(TARGET).bin
	@if [ -z "$(PORT)" ]; then \
	    echo "Usage: make flash PORT=/dev/ttyUSB0"; exit 1; fi
	$(ESPTOOL) --port $(PORT) --baud 460800 write_flash \
	    --flash_mode dio --flash_size 4MB \
	    0x00000 $(TARGET)-0x00000.bin

clean:
	rm -f $(OBJS_C0) $(OBJS_AS) $(OBJS_WA) $(TARGET).elf $(TARGET)-*.bin
