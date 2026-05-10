# =============================================================================
# KTOS ESP8266 WiFi Scan example — Makefile
#
# Requires: xtensa-lx106-elf toolchain (~/.local/bin or ~/esp/xtensa-lx106-elf/bin),
#           ESP8266 Non-OS SDK (~/esp/sdk), esptool.py
# Usage:
#   make              — build firmware binaries
#   make flash PORT=/dev/ttyUSB0
#   make clean
#
# Flash layout (4 MB flash, DIO mode, SPI_FLASH_SIZE_MAP=4):
#   0x00000  wifi_scan-0x00000.bin          (IRAM image — vectors, critical code)
#   0x10000  wifi_scan-0x10000.bin          (IROM image — bulk application code, XIP)
#   0x3FC000 esp_init_data_default_v08.bin  (PHY init data — required for RF cal)
#   0x3FB000 blank.bin                      (RF_CAL sector — SDK fills on first boot)
#   0x3FD000 blank.bin                      (system parameter sector)
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
INC       := -I$(CORE) -I$(SDK_DIR)/include -I.

# Base flags shared by all compilation units
BASE      := -mlongcalls -Os -Wall -Wextra -ffunction-sections -fdata-sections

# KTOS kernel + application code (windowed ABI — -mabi=call0 not supported by this toolchain)
C0FLAGS   := $(BASE) $(INC)

# startup.c: windowed ABI + flash size map define
#   SPI_FLASH_SIZE_MAP=4 → 32Mbit (4MB) flash, 512KB+512KB OTA map
#   esptool write_flash --flash_size 4MB writes header byte 0x40, which
#   the SDK decodes as spi_size_map=4 (FLASH_SIZE_32M_MAP_512_512).
#   Using map 6 here would cause "mismatch map 6,spi_size_map 4" at boot.
WAFLAGS   := $(BASE) $(INC) -DSPI_FLASH_SIZE_MAP=4

# ---------- Linker flags (eagle.app.v6.ld = IRAM/IROM split + ROM symbols) --
# The SDK's call_user_start (libmain) zeroes BSS, enables XIP cache, and calls
# user_pre_init() / user_init().  lwip provides pbuf/dhcp/netif symbols needed
# by the WiFi stack.
LDFLAGS   := -nostdlib                          \
             -L$(SDK_DIR)/lib                   \
             -Wl,-L$(SDK_DIR)/ld                \
             -Wl,--start-group                  \
               -lmain                           \
               -lnet80211                       \
               -lpp                             \
               -lphy                            \
               -lwpa                            \
               -llwip                           \
               -lcrypto                         \
               -lhal                            \
               -lc                              \
               -lgcc                            \
             -Wl,--end-group                    \
             -T$(SDK_DIR)/ld/eagle.app.v6.ld

# ---------- Sources ----------------------------------------------------------
# No alloc.c — SDK's malloc/calloc/free (from libmain) are used directly
# since all C code here is windowed ABI (compatible with the SDK's allocator).
SRCS_C0 := main.c            \
            $(CORE)/ktos.c    \
            $(BSP)/ktos_bsp.c

SRCS_AS := $(BSP)/ktos_bsp_asm.S

SRCS_WA := startup.c

OBJS_C0 := $(notdir $(SRCS_C0:.c=.o))
OBJS_AS := $(notdir $(SRCS_AS:.S=.o))
OBJS_WA := $(notdir $(SRCS_WA:.c=.o))

TARGET   := wifi_scan

# ---------- Targets ----------------------------------------------------------
.PHONY: all flash clean

all: $(TARGET).bin

$(TARGET).elf: $(OBJS_C0) $(OBJS_AS) $(OBJS_WA)
	$(CC) $(C0FLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET).bin: $(TARGET).elf
	$(ESPTOOL) elf2image --output $(TARGET)- $<
	@echo ""
	@echo "  Build OK:"
	@echo "    $(TARGET)-0x00000.bin  (flash at 0x00000)"
	@echo "    $(TARGET)-0x10000.bin  (flash at 0x10000)"
	@echo ""

ktos.o: $(CORE)/ktos.c
	$(CC) $(C0FLAGS) -c -o $@ $<

ktos_bsp.o: $(BSP)/ktos_bsp.c
	$(CC) $(C0FLAGS) -c -o $@ $<

main.o: main.c
	$(CC) $(C0FLAGS) -c -o $@ $<

ktos_bsp_asm.o: $(BSP)/ktos_bsp_asm.S
	$(CC) $(BASE) $(INC) -c -o $@ $<

startup.o: startup.c
	$(CC) $(WAFLAGS) -c -o $@ $<

flash: $(TARGET).bin
	@if [ -z "$(PORT)" ]; then \
	    echo "Usage: make flash PORT=/dev/ttyUSB0"; exit 1; fi
	$(ESPTOOL) --port $(PORT) --baud 460800 write_flash \
	    --flash_mode dio --flash_size 4MB   \
	    0x00000  $(TARGET)-0x00000.bin      \
	    0x10000  $(TARGET)-0x10000.bin      \
	    0x3FC000 $(SDK_DIR)/bin/esp_init_data_default_v08.bin \
	    0x3FB000 $(SDK_DIR)/bin/blank.bin   \
	    0x3FD000 $(SDK_DIR)/bin/blank.bin

clean:
	rm -f $(OBJS_C0) $(OBJS_AS) $(OBJS_WA) $(TARGET).elf $(TARGET)-*.bin
