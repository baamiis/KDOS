# =============================================================================
# KTOS ESP8266 HTTP(S) GET example — Makefile
#
# Requires: xtensa-lx106-elf toolchain, ESP8266 Non-OS SDK (~/esp/sdk),
#           esptool.py
# Usage:
#   make              — build firmware binaries
#   make flash PORT=/dev/ttyUSB0
#   make clean
#
# Before building: set credentials in wifi_config.h and endpoint in
# http_config.h.  Set HTTP_USE_SSL=1 and HTTP_PORT=443 for HTTPS.
#
# Flash layout (4 MB flash, DIO mode, SPI_FLASH_SIZE_MAP=4):
#   0x00000  https_get-0x00000.bin         (IRAM image)
#   0x10000  https_get-0x10000.bin         (IROM image, XIP)
#   0x3FC000 esp_init_data_default_v08.bin (PHY init data)
#   0x3FB000 blank.bin                     (RF_CAL sector)
#   0x3FD000 blank.bin                     (system parameter sector)
# =============================================================================

# ---------- Toolchain --------------------------------------------------------
CC       := xtensa-lx106-elf-gcc
ESPTOOL  := esptool.py

# ---------- SDK path ---------------------------------------------------------
SDK_DIR  ?= $(HOME)/esp/sdk

# ---------- Project paths ----------------------------------------------------
ROOT      := ../../..
CORE      := $(ROOT)/core
BSP       := $(ROOT)/bsp/esp8266

# ---------- Compiler flags ---------------------------------------------------
INC       := -I$(CORE) -I$(SDK_DIR)/include -I.

BASE      := -mlongcalls -Os -Wall -Wextra -ffunction-sections -fdata-sections

C0FLAGS   := $(BASE) $(INC)

WAFLAGS   := $(BASE) $(INC) -DSPI_FLASH_SIZE_MAP=4

# ---------- Linker flags -----------------------------------------------------
# -lssl provides espconn_secure_* (axTLS).  Some SDK builds bundle it inside
# -lmain; if the link fails with "cannot find -lssl", remove that line.
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
               -lssl                            \
               -lcrypto                         \
               -lhal                            \
               -lc                              \
               -lgcc                            \
             -Wl,--end-group                    \
             -T$(SDK_DIR)/ld/eagle.app.v6.ld

# ---------- Sources ----------------------------------------------------------
SRCS_C0 := main.c            \
            $(CORE)/ktos.c    \
            $(BSP)/ktos_bsp.c

SRCS_AS := $(BSP)/ktos_bsp_asm.S

SRCS_WA := startup.c

OBJS_C0 := $(notdir $(SRCS_C0:.c=.o))
OBJS_AS := $(notdir $(SRCS_AS:.S=.o))
OBJS_WA := $(notdir $(SRCS_WA:.c=.o))

TARGET   := https_get

# ---------- Targets ----------------------------------------------------------
# ---------- Certificate flash addresses --------------------------------------
CA_FLASH_ADDR     := 0x0EC000
CLIENT_FLASH_ADDR := 0x0ED000

.PHONY: all flash flash_ca_cert flash_client_cert clean

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

# Flash the CA cert image (server verification).
# Usage: make flash_ca_cert PORT=/dev/ttyUSB0 CA_PEM=ca.pem
flash_ca_cert:
	@if [ -z "$(PORT)" ];   then echo "Usage: make flash_ca_cert PORT=/dev/ttyUSB0 CA_PEM=<file>"; exit 1; fi
	@if [ -z "$(CA_PEM)" ]; then echo "CA_PEM not set — supply the CA certificate PEM file"; exit 1; fi
	python3 cert_to_flash.py ca $(CA_PEM) ca_flash.bin
	$(ESPTOOL) --port $(PORT) --baud 460800 write_flash \
	    --flash_mode dio --flash_size 4MB \
	    $(CA_FLASH_ADDR) ca_flash.bin

# Flash the client cert + private key image (mutual TLS).
# Usage: make flash_client_cert PORT=/dev/ttyUSB0 CLIENT_PEM=client.pem CLIENT_KEY=client_key.pem
flash_client_cert:
	@if [ -z "$(PORT)" ];        then echo "Usage: make flash_client_cert PORT=/dev/ttyUSB0 CLIENT_PEM=<file> CLIENT_KEY=<file>"; exit 1; fi
	@if [ -z "$(CLIENT_PEM)" ];  then echo "CLIENT_PEM not set"; exit 1; fi
	@if [ -z "$(CLIENT_KEY)" ];  then echo "CLIENT_KEY not set"; exit 1; fi
	python3 cert_to_flash.py client $(CLIENT_PEM) $(CLIENT_KEY) client_flash.bin
	$(ESPTOOL) --port $(PORT) --baud 460800 write_flash \
	    --flash_mode dio --flash_size 4MB \
	    $(CLIENT_FLASH_ADDR) client_flash.bin

clean:
	rm -f $(OBJS_C0) $(OBJS_AS) $(OBJS_WA) $(TARGET).elf $(TARGET)-*.bin \
	      ca_flash.bin client_flash.bin
