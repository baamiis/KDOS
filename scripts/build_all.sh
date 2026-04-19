#!/bin/bash
# Build all KTOS BSPs locally before pushing.
# Run: bash scripts/build_all.sh

set -e

PASS=0
FAIL=0
ERRORS=()

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

check_tool() {
    if ! command -v "$1" &>/dev/null; then
        echo -e "${RED}Missing toolchain: $1${NC}"
        echo "Install with: sudo apt-get install $2"
        exit 1
    fi
}

echo "=========================================="
echo " KTOS local build verification"
echo "=========================================="

# Check toolchains
check_tool arm-none-eabi-gcc "gcc-arm-none-eabi libnewlib-arm-none-eabi"
check_tool avr-gcc           "gcc-avr avr-libc"

build_bsp() {
    local bsp=$1
    local dir="$ROOT/bsp/$bsp"
    echo -n "Building $bsp ... "
    if make -C "$dir" clean all 2>/tmp/ktos_build_$bsp.log; then
        echo -e "${GREEN}PASS${NC}"
        PASS=$((PASS + 1))
    else
        echo -e "${RED}FAIL${NC}"
        echo -e "${YELLOW}--- Error log for $bsp ---${NC}"
        cat /tmp/ktos_build_$bsp.log
        echo -e "${YELLOW}--------------------------${NC}"
        FAIL=$((FAIL + 1))
        ERRORS+=("$bsp")
    fi
}

# Build every BSP found in bsp/
for bsp_dir in "$ROOT"/bsp/*/; do
    bsp=$(basename "$bsp_dir")
    # Skip ESP8266 if xtensa toolchain not installed
    if [ "$bsp" = "esp8266" ] && ! command -v xtensa-lx106-elf-gcc &>/dev/null; then
        echo -e "Skipping esp8266 ${YELLOW}(xtensa toolchain not installed)${NC}"
        continue
    fi
    build_bsp "$bsp"
done

echo ""
echo "=========================================="
echo " Results: ${GREEN}${PASS} passed${NC}  ${RED}${FAIL} failed${NC}"
echo "=========================================="

if [ ${#ERRORS[@]} -gt 0 ]; then
    echo -e "${RED}Failed BSPs: ${ERRORS[*]}${NC}"
    echo "Fix errors before pushing."
    exit 1
fi

echo -e "${GREEN}All builds passed. Safe to push.${NC}"
exit 0
