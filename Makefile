#
# TBVM (A fork of TinyEMU)
# 
# Copyright (c) 2016-2018 Fabrice Bellard
# Copyright (c) 2019 Erdem Meydanli
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#

# ########### Toolchain -----------------------------

CROSS_PREFIX=

CC       = $(CROSS_PREFIX)gcc
STRIP    = $(CROSS_PREFIX)strip
CFLAGS   = -Os -Wall -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -MMD
ifeq ($(DEBUG),1)
CFLAGS   = -ggdb3
endif
CFLAGS  += -D_GNU_SOURCE -DCONFIG_VERSION=\"$(shell cat VERSION)\"
LDFLAGS  =

# ########### Sources & Libraries -----------------------------

WORKING_DIRECTORY := $(dir $(realpath $(firstword $(MAKEFILE_LIST))))

DEST_DIR  = /usr/local/bin
BUILD_DIR = $(WORKING_DIRECTORY)build
INSTALL   = install
APPS      = tbvm

CFLAGS   += -DCONFIG_RISCV_MAX_XLEN=32
EMU_OBJS := virtio.o cutils.o iomem.o fs_disk.o json.o machine.o temu.o riscv_machine.o softfp.o riscv_cpu32.o 
EMU_OBJS := $(addprefix $(BUILD_DIR)/, $(EMU_OBJS))

EMU_LIBS = -lrt

.PHONY: run prepare

all: prepare $(APPS)

prepare:
	mkdir -p $(BUILD_DIR)

tbvm: $(EMU_OBJS)
	$(CC) $(LDFLAGS) -o $(BUILD_DIR)/$@ $^ $(EMU_LIBS)

$(BUILD_DIR)/riscv_cpu32.o: riscv_cpu.c
	$(CC) $(CFLAGS) -DMAX_XLEN=32 -c -o $@ $<

install: $(APPS)
	$(STRIP) $(PROGS)
	$(INSTALL) -m755 $(APPS) "$(DEST_DIR)"

$(BUILD_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(BUILD_DIR)/*

run: all
	$(BUILD_DIR)/$(APPS) $(WORKING_DIRECTORY)/demo/profiles/default.prd

-include $(wildcard *.d)
