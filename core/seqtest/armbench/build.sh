#!/bin/sh
# usage: build.sh <src.cpp> <include root (core dir)> <out.elf> [extra flags]
SRC=$1; ROOT=$2; OUT=$3; shift 3
D=$(dirname "$0")
arm-none-eabi-g++ -std=gnu++17 -fno-rtti -fno-exceptions -fno-threadsafe-statics -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -O2 -ffunction-sections -fdata-sections -ffreestanding -fno-builtin "$@" -I "$ROOT/libs/Audio" -I "$ROOT/source" -nostdlib -T "$D/link.ld" -Wl,--gc-sections -Wl,-u,setup -Wl,-u,run -Wl,-u,hash -Wl,-u,voices -o "$OUT" "$SRC" "$D/../../../platforms/mk66/source/fastmem.S" -lgcc
