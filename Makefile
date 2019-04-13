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
AR       = $(CROSS_PREFIX)ar

CFLAGS   = -Wall -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -MMD
ifeq ($(DEBUG),1)
CFLAGS   += -ggdb3 
else
CFLAGS   += -Os -DDISABLE_CONSOLE
endif
CFLAGS  += -D_GNU_SOURCE -DCONFIG_VERSION=\"$(shell cat VERSION)\"
LDFLAGS  =

LIBS     = -lrt
# ########### Sources & Libraries -----------------------------

WORKING_DIRECTORY := $(dir $(realpath $(firstword $(MAKEFILE_LIST))))
BUILT_SHARED_FLAG  = $(BUILD_DIR)/.built_shared
BUILT_STATIC_FLAG  = $(BUILD_DIR)/.built_static

ifeq ($(SHARED),1)
CFLAGS   += -fPIC
DEST_DIR ?= /usr/local/lib
LDFLAGS   = -shared
OUTPUT    = libtbvm.so
else ifeq ($(STATIC), 1)
DEST_DIR ?= /usr/local/lib
OUTPUT    = libtbvm.a
else
DEST_DIR  ?= /usr/local/bin
OUTPUT    = tbvm
LIBS      = -ltbvm
LDFLAGS   = -L/usr/local/lib -L/usr/lib 
ifeq ($(DEBUG),1)
LDFLAGS  += -L$(WORKING_DIRECTORY)/build
endif
endif

BUILD_DIR = $(WORKING_DIRECTORY)build
INSTALL   = install

CFLAGS   += -DCONFIG_RISCV_MAX_XLEN=32

ifneq ($(OUTPUT), tbvm)
OBJS     := virtio.o cutils.o iomem.o fs_disk.o machine.o tbvm.o riscv_machine.o softfp.o riscv_cpu32.o 
else
OBJS     := main.o
endif

OBJS     := $(addprefix $(BUILD_DIR)/, $(OBJS))

.PHONY: run prepare dev


all: prepare $(OUTPUT)
ifneq ($(DEBUG),1)
	$(STRIP) $(BUILD_DIR)/$(OUTPUT)
endif

dev:
	$(MAKE) -C $(WORKING_DIRECTORY) SHARED=1 DEBUG=1
	$(MAKE) -C $(WORKING_DIRECTORY) DEBUG=1 run

prepare:
	mkdir -p $(BUILD_DIR)
ifeq ($(SHARED),1)
ifneq ("$(wildcard $(BUILT_STATIC_FLAG))","")
#Already built for static library. Rebuild required.
	$(info Cleaning up obsolete static library objects...)
	$(MAKE) clean
endif
else ifeq ($(STATIC), 1)
ifneq ("$(wildcard $(BUILT_SHARED_FLAG))","")
#Already built for shared library. Rebuild required.
	$(info Cleaning up obsolete shared library objects...)
	$(MAKE) clean
endif
endif


$(OUTPUT): $(OBJS)
ifneq ($(STATIC), 1)
	$(CC) $(LDFLAGS) -o $(BUILD_DIR)/$@ $^ $(LIBS)
ifeq ($(SHARED), 1)
	touch $(BUILT_SHARED_FLAG)
endif
else
	$(AR) rcs $(BUILD_DIR)/$@ $^
	touch $(BUILT_STATIC_FLAG)
endif

$(BUILD_DIR)/riscv_cpu32.o: riscv_cpu.c
	$(CC) $(CFLAGS) -DMAX_XLEN=32 -c -o $@ $<

install: 
	$(INSTALL) -m755 $(BUILD_DIR)/$(OUTPUT) "$(DEST_DIR)"

$(BUILD_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(BUILD_DIR)/*

run: all
	LD_LIBRARY_PATH=$(BUILD_DIR) $(BUILD_DIR)/tbvm $(WORKING_DIRECTORY)/demo/profiles/default.prd

-include $(wildcard *.d)
