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
ifeq ($(USE_SSTRIP),1)
STRIP    = sstrip
else
STRIP    = $(CROSS_PREFIX)strip
endif
AR       = $(CROSS_PREFIX)ar
INSTALL  = install

CFLAGS   += -O0 -ggdb3 -D_GNU_SOURCE
LIBS     = -lm

# ########### Sources & Libraries -----------------------------

WORKING_DIRECTORY := $(dir $(realpath $(firstword $(MAKEFILE_LIST))))

MINIZ_PATH := $(WORKING_DIRECTORY)../../external/miniz
CFLAGS     += -I$(MINIZ_PATH) -I$(WORKING_DIRECTORY)/../..
BUILD_DIR   = $(WORKING_DIRECTORY)build

OBJS       := main.o miniz.o miniz_tdef.o miniz_tinfl.o
OBJS       := $(addprefix $(BUILD_DIR)/, $(OBJS))

OUTPUT      = tcomp

.PHONY: run prepare dev

all: prepare $(OUTPUT)

prepare:
	mkdir -p $(BUILD_DIR)

$(OUTPUT): $(OBJS)
	$(CC) $(LDFLAGS) -o $(BUILD_DIR)/$@ $^ $(LIBS)

install: 
	$(INSTALL) -m755 $(BUILD_DIR)/$(OUTPUT) "$(DEST_DIR)"

$(BUILD_DIR)/%.o: $(MINIZ_PATH)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(BUILD_DIR)/*

-include $(wildcard *.d)
