# SPDX-License-Identifier: GPL-3.0-or-later
CC ?= cc
CFLAGS ?= -O2 -march=x86-64 -mtune=generic
COMMON_INCLUDE ?= Libraries/include
OBJCOPY ?= objcopy
BASE = -std=c11 -Wall -Wextra -Werror -I$(COMMON_INCLUDE)
SRC = native/filesystems/hpfs/hpfs.c
.PHONY: all firmware carrier check clean
all: build/HPFS.so
build/HPFS.so: $(SRC) Makefile
	mkdir -p build
	$(CC) $(BASE) $(CFLAGS) -fPIC -fvisibility=hidden -shared -Wl,-z,defs -o $@ $<
firmware: build/firmware/HPFS.so
build/firmware/HPFS.so: $(SRC) module/firmware/CMakeLists.txt Makefile
	cmake -S module/firmware -B build/firmware -DCMAKE_C_FLAGS= -DCOMMON_INCLUDE_DIR="$(abspath $(COMMON_INCLUDE))"
	cmake --build build/firmware
	test "$$(nm -D --defined-only $@ | awk '{print $$3}')" = infiltrator_filesystem_api_v2
	test -z "$$(nm -D --undefined-only $@)"
carrier: all firmware
	mkdir -p build/carrier
	$(OBJCOPY) --add-section .infiltrator_boot=build/firmware/HPFS.so --set-section-flags .infiltrator_boot=readonly build/HPFS.so build/carrier/HPFS.so
	$(OBJCOPY) --dump-section .infiltrator_boot=build/carrier/verify.elf build/carrier/HPFS.so build/carrier/verify-carrier.elf
	cmp build/firmware/HPFS.so build/carrier/verify.elf
	rm build/carrier/verify.elf build/carrier/verify-carrier.elf
build/test_hpfs: tests/test_hpfs.c Makefile
	mkdir -p build
	$(CC) $(BASE) $(CFLAGS) -o $@ $< -ldl
check: carrier build/test_hpfs
	build/test_hpfs ./build/carrier/HPFS.so
	build/test_hpfs ./build/firmware/HPFS.so
clean:
	rm -rf build
