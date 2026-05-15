# =============================================================================
# KTOS ESP8266 BearSSL TLS 1.2 HTTPS GET example — Makefile
#
# Pre-built library and trust anchors are included in lib/.
# No setup required — just edit wifi_config.h, then:
#
#   make
#   make flash PORT=/dev/ttyUSB0
#   make clean
#
# Requires: xtensa-lx106-elf toolchain, ESP8266 Non-OS SDK, esptool.py
#
# To add a different CA (self-signed server, different root):
#   pip install cryptography
#   python3 make_trust_anchors.py your_ca.pem >> trust_anchors.h
#   (then update TAs_NUM in trust_anchors.h to match the new count)
# =============================================================================

# ---------- Toolchain ---------------------------------------------------------
CC      := xtensa-lx106-elf-gcc
ESPTOOL := esptool.py

# ---------- Paths -------------------------------------------------------------
SDK_DIR ?= $(HOME)/esp/sdk
ROOT    := ../../..
CORE    := $(ROOT)/core
BSP     := $(ROOT)/bsp/esp8266

# Pre-built BearSSL static library and headers (committed in lib/)
BEAR_LIB := lib/libbearssl.a
BEAR_INC := lib/bearssl_inc

# ---------- Compiler flags ----------------------------------------------------
INC := -I$(CORE) -I$(SDK_DIR)/include -I$(BEAR_INC) -I.

BASE    := -mlongcalls -Os -Wall -Wextra -ffunction-sections -fdata-sections
C0FLAGS := $(BASE) $(INC)
WAFLAGS := $(BASE) $(INC) -DSPI_FLASH_SIZE_MAP=4

# ---------- Linker flags ------------------------------------------------------
LDFLAGS := -nostdlib                        \
           -L$(SDK_DIR)/lib                 \
           -Wl,-L$(SDK_DIR)/ld              \
           -Wl,--start-group               \
             -lmain                         \
             -lnet80211                     \
             -lpp                           \
             -lphy                          \
             -lwpa                          \
             -llwip                         \
             -lcrypto                       \
             -lhal                          \
             -lc                            \
             -lgcc                          \
           -Wl,--end-group                  \
           -T eagle_bear.ld

# ---------- Sources -----------------------------------------------------------
SRCS_C0 := main.c             \
            bearssl_conn.c     \
            $(CORE)/ktos.c     \
            $(BSP)/ktos_bsp.c

SRCS_AS := $(BSP)/ktos_bsp_asm.S
SRCS_WA := startup.c

OBJS_C0 := $(notdir $(SRCS_C0:.c=.o))
OBJS_AS := $(notdir $(SRCS_AS:.S=.o))
OBJS_WA := $(notdir $(SRCS_WA:.c=.o))

TARGET := https_bear_ssl

# ---------- Build -------------------------------------------------------------
.PHONY: all flash flash_full clean

all: $(TARGET).bin

$(TARGET).elf: $(OBJS_C0) $(OBJS_AS) $(OBJS_WA)
	$(CC) $(C0FLAGS) -o $@ $^ $(BEAR_LIB) $(LDFLAGS)

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

ktos_bsp_asm.o: $(BSP)/ktos_bsp_asm.S
	$(CC) $(BASE) $(INC) -c -o $@ $<

main.o: main.c trust_anchors.h
	$(CC) $(C0FLAGS) -c -o $@ main.c

bearssl_conn.o: bearssl_conn.c trust_anchors.h
	$(CC) $(C0FLAGS) -c -o $@ bearssl_conn.c

startup.o: startup.c
	$(CC) $(WAFLAGS) -c -o $@ $<

# ---------- Flash -------------------------------------------------------------
flash: $(TARGET).bin
	@if [ -z "$(PORT)" ]; then \
	    echo "Usage: make flash PORT=/dev/ttyUSB0"; exit 1; fi
	$(ESPTOOL) --port $(PORT) --baud 460800 write_flash \
	    --flash_mode dio --flash_size 4MB   \
	    0x00000  $(TARGET)-0x00000.bin      \
	    0x10000  $(TARGET)-0x10000.bin      \
	    0x3FC000 $(SDK_DIR)/bin/esp_init_data_default_v08.bin \
	    0x3FD000 $(SDK_DIR)/bin/blank.bin

# flash_full: first-time / after chip-erase only.
# Writes RF calibration (0x3FB000) and system-parameter (0x3FD000) sectors.
# Do NOT use for routine firmware updates — it resets WiFi calibration data.
flash_full: $(TARGET).bin
	@if [ -z "$(PORT)" ]; then \
	    echo "Usage: make flash_full PORT=/dev/ttyUSB0"; exit 1; fi
	$(ESPTOOL) --port $(PORT) --baud 460800 write_flash \
	    --flash_mode dio --flash_size 4MB   \
	    0x00000  $(TARGET)-0x00000.bin      \
	    0x10000  $(TARGET)-0x10000.bin      \
	    0x3FC000 $(SDK_DIR)/bin/esp_init_data_default_v08.bin \
	    0x3FB000 $(SDK_DIR)/bin/blank.bin   \
	    0x3FD000 $(SDK_DIR)/bin/blank.bin

clean:
	rm -f $(OBJS_C0) $(OBJS_AS) $(OBJS_WA) $(TARGET).elf $(TARGET)-*.bin
