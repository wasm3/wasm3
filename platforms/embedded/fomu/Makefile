GIT_VERSION := $(shell git describe --tags)

# There is no 64-bit gcc on Raspberry Pi, so use the 32-bit version
ifneq (,$(wildcard /etc/rpi-issue))
TRGT       ?= riscv32-unknown-elf-
else
TRGT       ?= riscv64-unknown-elf-
endif

CC         := $(TRGT)gcc
CXX        := $(TRGT)g++
OBJCOPY    := $(TRGT)objcopy

RM         := rm -rf
COPY       := cp -a
PATH_SEP   := /

ifeq ($(OS),Windows_NT)
COPY       := copy
RM         := del
PATH_SEP   := \\
endif

BASE_DIR   := .
LD_DIR     := $(BASE_DIR)/ld
LDSCRIPT   := $(BASE_DIR)/ld/linker.ld
ADD_CFLAGS := -I$(BASE_DIR)/include -I$(BASE_DIR)/src/wasm3/ -D__vexriscv__ \
			  -DFOMU -Dd_m3FixedHeap=8192

ADD_LFLAGS := -lm
PACKAGE    := wasm3

LTO        := -flto -Wl,--allow-multiple-definition

LDSCRIPTS  := $(LDSCRIPT) $(LD_DIR)/output_format.ld $(LD_DIR)/regions.ld
SRC_DIR    := $(BASE_DIR)/src
DBG_CFLAGS := -ggdb -g -DDEBUG -Wall
DBG_LFLAGS := -ggdb -g -Wall
CFLAGS     := $(ADD_CFLAGS) \
			  -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable \
			  -Wno-missing-field-initializers \
			  -ffunction-sections -fdata-sections -fno-common \
			  -fomit-frame-pointer -Os $(LTO) \
			  -march=rv32i -mabi=ilp32 \
			  -DGIT_VERSION=u\"$(GIT_VERSION)\" -std=gnu11
CXXFLAGS   := $(CFLAGS) -std=c++11 -fno-rtti -fno-exceptions
LFLAGS     := $(CFLAGS) $(ADD_LFLAGS) -L$(LD_DIR) \
			  -nostartfiles $(LTO) \
			  -Wl,--gc-sections \
			  -Wl,--script=$(LDSCRIPT) \
			  -Wl,--build-id=none

OBJ_DIR    := .obj

CSOURCES   := $(shell find -L $(SRC_DIR) -type f -name '*.c')
CPPSOURCES := $(shell find -L $(SRC_DIR) -type f -name '*.cpp')
ASOURCES   := $(shell find -L $(SRC_DIR) -type f -name '*.S')
COBJS      := $(addprefix $(OBJ_DIR)/,$(CSOURCES:.c=.o))
CXXOBJS    := $(addprefix $(OBJ_DIR)/,$(CPPSOURCES:.cpp=.o))
AOBJS      := $(addprefix $(OBJ_DIR)/,$(ASOURCES:.S=.o))
OBJECTS    := $(COBJS) $(CXXOBJS) $(AOBJS)
VPATH      := $(SRC_DIR)

QUIET      := @

ALL        := all
TARGET     := $(PACKAGE).elf
CLEAN      := clean

$(ALL): $(TARGET) $(PACKAGE).bin $(PACKAGE).ihex $(PACKAGE).dfu

#$(OBJECTS): | $(OBJ_DIR)

$(TARGET): $(OBJECTS) $(LDSCRIPTS)
	$(QUIET) echo "  LD       $@"
	$(QUIET) $(CC) $(OBJECTS) $(LFLAGS) -o $@

$(PACKAGE).bin: $(TARGET)
	$(QUIET) echo "  OBJCOPY  $@"
	$(QUIET) $(OBJCOPY) -O binary $(TARGET) $@

$(PACKAGE).dfu: $(PACKAGE).bin
	$(QUIET) echo "  DFU      $@"
	$(QUIET) $(COPY) $(PACKAGE).bin $@
	$(QUIET) dfu-suffix -v 1209 -p 70b1 -a $@

$(PACKAGE).ihex: $(TARGET)
	$(QUIET) echo "  IHEX     $(PACKAGE).ihex"
	$(QUIET) $(OBJCOPY) -O ihex $(TARGET) $@

$(DEBUG): CFLAGS += $(DBG_CFLAGS)
$(DEBUG): LFLAGS += $(DBG_LFLAGS)
CFLAGS += $(DBG_CFLAGS)
LFLAGS += $(DBG_LFLAGS)
$(DEBUG): $(TARGET)

$(OBJ_DIR):
	$(QUIET) mkdir $(OBJ_DIR)

$(COBJS) : $(OBJ_DIR)/%.o : %.c $(BASE_DIR)/Makefile
	$(QUIET) mkdir -p $(@D)
	$(QUIET) echo "  CC       $<	$@"
	$(QUIET) $(CC) -c $< $(CFLAGS) -o $@ -MMD

$(OBJ_DIR)/%.o: %.cpp
	$(QUIET) mkdir -p $(@D)
	$(QUIET) echo "  CXX      $<	$@"
	$(QUIET) $(CXX) -c $< $(CXXFLAGS) -o $@ -MMD

$(OBJ_DIR)/%.o: %.S
	$(QUIET) mkdir -p $(@D)
	$(QUIET) echo "  AS       $<	$@"
	$(QUIET) $(CC) -x assembler-with-cpp -c $< $(CFLAGS) -o $@ -MMD

.PHONY: clean

clean:
	- $(RM) -rf $(OBJ_DIR)
	- $(RM) $(TARGET) $(PACKAGE).bin $(PACKAGE).symbol $(PACKAGE).ihex $(PACKAGE).dfu

-include $(OBJECTS:.o=.d)
