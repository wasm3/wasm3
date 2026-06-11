#############################################################
# Required variables for each makefile
# Discard this section from all parent makefiles
# Expected variables (with automatic defaults):
#   CSRCS (all "C" files in the dir)
#   SUBDIRS (all subdirs with a Makefile)
#   GEN_LIBS - list of libs to be generated ()
#   GEN_IMAGES - list of object file images to be generated ()
#   GEN_BINS - list of binaries to be generated ()
#   COMPONENTS_xxx - a list of libs/objs in the form
#     subdir/lib to be extracted and rolled up into
#     a generated lib/image xxx.a ()
#

TOP_DIR:= $(WM_SDK)

sinclude $(TOP_DIR)/tools/tool_chain.def

USE_LIB=1

CSRCS = $(wildcard *.c) $(wildcard wasm3/*.c)

#EXTRA_CCFLAGS += -u
ifndef PDIR
GEN_IMAGES= $(TARGET).out
GEN_BINS = $(TARGET).bin
SUBDIRS = \
	$(TOP_DIR)/platform/boot/$(COMPILE)

ifeq ($(USE_LIB), 0)
SUBDIRS += \
	$(TOP_DIR)/platform/common 		\
	$(TOP_DIR)/platform/drivers		\
	$(TOP_DIR)/platform/sys			\
	$(TOP_DIR)/src/os				\
	$(TOP_DIR)/src/app				\
	$(TOP_DIR)/src/network
endif
endif

COMPONENTS_$(TARGET) =	\
	$(TOP_DIR)/platform/boot/$(COMPILE)/startup.o	\
	$(TOP_DIR)/platform/boot/$(COMPILE)/misc.o	\
	$(TOP_DIR)/platform/boot/$(COMPILE)/retarget.o

ifeq ($(USE_LIB), 0)
COMPONENTS_$(TARGET) += \
	$(TOP_DIR)/platform/common/libcommon$(LIB_EXT)		\
	$(TOP_DIR)/platform/drivers/libdrivers$(LIB_EXT)		\
	$(TOP_DIR)/platform/sys/libsys$(LIB_EXT)			\
	$(TOP_DIR)/src/os/libos$(LIB_EXT)					\
	$(TOP_DIR)/src/network/libnetwork$(LIB_EXT)					\
	$(TOP_DIR)/src/app/libapp$(LIB_EXT)
endif

ifeq ($(USE_LIB), 1)
LINKLIB = \
	$(TOP_DIR)/lib/libcommon$(LIB_EXT) 		\
	$(TOP_DIR)/lib/libdrivers$(LIB_EXT)		\
	$(TOP_DIR)/lib/libsys$(LIB_EXT)			\
	$(TOP_DIR)/lib/libos$(LIB_EXT)			\
	$(TOP_DIR)/lib/libnetwork$(LIB_EXT)		\
	$(TOP_DIR)/lib/libapp$(LIB_EXT)
endif

LINKLIB += 	\
	$(TOP_DIR)/lib/libairkiss_log$(LIB_EXT)	\
	$(TOP_DIR)/lib/libwlan$(LIB_EXT) 
	
ifeq ($(COMPILE), gcc)
LINKFLAGS_$(TARGET) =  \
	$(LINKLIB)	\
	-T$(LD_FILE)	\
	-Wl,-warn-common
else
LINKFLAGS_$(TARGET) = 	\
	--library_type=microlib	\
	$(LINKLIB)	\
	--strict --scatter $(LD_FILE)
endif

#############################################################
# Configuration i.e. compile options etc.
# Target specific stuff (defines etc.) goes in here!
# Generally values applying to a tree are captured in the
#   makefile at its root level - these are then overridden
#   for a subtree within the makefile rooted therein
#

CONFIGURATION_DEFINES += -DWM_W600

DEFINES += $(CONFIGURATION_DEFINES) -Os -flto -Wfatal-errors \
			-fomit-frame-pointer -fno-stack-check -fno-stack-protector \
			-Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -Wno-missing-field-initializers
#			-Dd_m3FixedHeap=8192
#			-fno-optimize-sibling-calls

LINKFLAGS_$(TARGET) += -Os -flto

LINKLIB += -lm

#############################################################
# Recursion Magic - Don't touch this!!
#
# Each subtree potentially has an include directory
#   corresponding to the common APIs applicable to modules
#   rooted at that subtree. Accordingly, the INCLUDE PATH
#   of a module can only contain the include directories up
#   its parent path, and not its siblings
#
# Required for each makefile to inherit from the parent
#

INCLUDES := $(INCLUDES) -I$(PDIR)include
INCLUDES += -I ./ -I ./wasm3/

sinclude $(TOP_DIR)/tools/rules.mk

.PHONY: FORCE
FORCE:
