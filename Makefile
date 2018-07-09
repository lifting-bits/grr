# Copyright 2015 Peter Goodman, all rights reserved.

.PHONY: all clean

GRANARY_CC ?= clang-3.8
GRANARY_CXX ?= clang++-3.8

# Where is Granary's source code located?
GRANARY_SRC_DIR ?= $(shell pwd)
GRANARY_LIB_DIR := $(GRANARY_SRC_DIR)/third_party
GRANARY_GEN_DIR := $(GRANARY_SRC_DIR)/gen

# What OS are we compiling for?
GRANARY_OS ?= decree

# Where will Granary run? Specify `kernel` for kernel space, and `user` for
# user space.
GRANARY_WHERE ?= user

# What type of build should be perform?
GRANARY_TARGET ?= debug

# Useful for distinguishing different kinds of builds.
GRANARY_TRIPLE := $(GRANARY_TARGET)_$(GRANARY_OS)_$(GRANARY_WHERE)

# Where should we emit object files and the executable?
GRANARY_BIN_DIR ?= $(GRANARY_SRC_DIR)/bin/$(GRANARY_TRIPLE)

# Should we assume that Granary will be executed with Valgrind?
GRANARY_WITH_VALGRIND ?= 0

# Compiler warnings that are explicitly disabled.
GRANARY_DISABLED_WARNINGS := -Wno-gnu-anonymous-struct
GRANARY_DISABLED_WARNINGS += -Wno-gnu-conditional-omitted-operand
GRANARY_DISABLED_WARNINGS += -Wno-long-long
GRANARY_DISABLED_WARNINGS += -Wno-gnu-statement-expression
GRANARY_DISABLED_WARNINGS += -Wno-nested-anon-types
GRANARY_DISABLED_WARNINGS += -Wno-extended-offsetof
GRANARY_DISABLED_WARNINGS += -Wno-c++98-compat-pedantic -Wno-c++98-compat
GRANARY_DISABLED_WARNINGS += -Wno-padded
GRANARY_DISABLED_WARNINGS += -Wno-unused-macros
GRANARY_DISABLED_WARNINGS += -Wno-missing-variable-declarations
GRANARY_DISABLED_WARNINGS += -Wno-missing-prototypes
GRANARY_DISABLED_WARNINGS += -Wno-packed
GRANARY_DISABLED_WARNINGS += -Wno-global-constructors
GRANARY_DISABLED_WARNINGS += -Wno-exit-time-destructors
GRANARY_DISABLED_WARNINGS += -Wno-disabled-macro-expansion
GRANARY_DISABLED_WARNINGS += -Wno-date-time
GRANARY_DISABLED_WARNINGS += -Wno-reserved-id-macro

# Arch-specific flags.
GRANARY_ARCH_FLAGS := -m64 -mtune=generic -fPIC -ffreestanding
GRANARY_ARCH_FLAGS += -ftls-model=initial-exec -mno-red-zone
GRANARY_ARCH_FLAGS += -fno-common -fno-builtin
GRANARY_ARCH_FLAGS += -fno-stack-protector -minline-all-stringops

# Flags that are common to both C and C++ compilers.
GRANARY_COMMON_FLAGS :=
GRANARY_COMMON_FLAGS += -I$(GRANARY_SRC_DIR)
GRANARY_COMMON_FLAGS += -Wall -Werror -Wpedantic 
GRANARY_COMMON_FLAGS += $(GRANARY_DISABLED_WARNINGS)
GRANARY_COMMON_FLAGS += -DGRANARY_WHERE_$(GRANARY_WHERE)
GRANARY_COMMON_FLAGS += -DGRANARY_OS_$(GRANARY_OS)
GRANARY_COMMON_FLAGS += -DGRANARY_TARGET_$(GRANARY_TARGET)
GRANARY_COMMON_FLAGS += -DGOOGLE_PROTOBUF_NO_RTTI

GRANARY_SANITIZER ?=

# Optimization and debug information level.
ifeq (debug,$(GRANARY_TARGET))
	GRANARY_COMMON_FLAGS += -O0 -g3 -fno-inline
	ifneq (,$(GRANARY_SANITIZER))
		GRANARY_COMMON_FLAGS += -fsanitize=$(GRANARY_SANITIZER)
	endif
else
	GRANARY_COMMON_FLAGS += -Oz -g3
endif

# Flags to pass to the various compilers.
GRANARY_CC_FLAGS := -std=c11 $(GRANARY_COMMON_FLAGS) $(GRANARY_ARCH_FLAGS)
GRANARY_CXX_FLAGS := -std=c++11
GRANARY_CXX_FLAGS += $(GRANARY_COMMON_FLAGS) $(GRANARY_ARCH_FLAGS)
GRANARY_CXX_FLAGS += -fno-exceptions -fno-asynchronous-unwind-tables -fno-rtti
GRANARY_CXX_FLAGS += -isystem $(GRANARY_LIB_DIR)/gflags/include

# C, C++, and assembly files in Granary.
GRANARY_SRC_FILES := $(shell find $(GRANARY_SRC_DIR)/granary/ -name '*.cc' -or -name '*.c' -or -name '*.S' -type f)
GRANARY_SRC_FILES += $(shell find $(GRANARY_SRC_DIR)/third_party/ -name '*.cc' -or -name '*.c' -or -name '*.S' -type f)

DUMP_SRC_FILES = $(GRANARY_SRC_DIR)/coverage.cc
DUMP_SRC_FILES += $(GRANARY_SRC_DIR)/granary/code/index.cc
DUMP_SRC_FILES += $(GRANARY_SRC_DIR)/granary/base/breakpoint.cc
DUMP_SRC_FILES += $(GRANARY_SRC_DIR)/granary/base/interrupt.cc
DUMP_SRC_FILES += $(GRANARY_LIB_DIR)/xxhash/xxhash.c
DUMP_OBJECT_FILES := $(addsuffix .o, $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(DUMP_SRC_FILES)))

PLAY_SRC_FILES = $(GRANARY_SRC_DIR)/play.cc $(GRANARY_SRC_FILES)
PLAY_OBJECT_FILES := $(addsuffix .o, $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(PLAY_SRC_FILES)))

SNAPSHOT_SRC_FILES := $(GRANARY_SRC_DIR)/snapshot.cc
SNAPSHOT_SRC_FILES += $(GRANARY_SRC_DIR)/granary/os/snapshot.cc
SNAPSHOT_SRC_FILES += $(GRANARY_SRC_DIR)/granary/os/decree_user/snapshot.cc
SNAPSHOT_SRC_FILES += $(GRANARY_SRC_DIR)/granary/base/breakpoint.cc
SNAPSHOT_SRC_FILES += $(GRANARY_SRC_DIR)/granary/base/interrupt.cc
SNAPSHOT_OBJ_FILES := $(addsuffix .o, $(subst $(GRANARY_SRC_DIR),$(GRANARY_BIN_DIR),$(SNAPSHOT_SRC_FILES)))

# Compile C++ files to object files.
$(GRANARY_BIN_DIR)/%.pb.cc.o :: $(GRANARY_SRC_DIR)/%.pb.cc
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	@$(GRANARY_CXX) -Weverything -Wno-sign-conversion -Wno-shorten-64-to-32 $(GRANARY_CXX_FLAGS) -c $< -o $@

# Compile C++ files to object files.
$(GRANARY_BIN_DIR)/%.cc.o :: $(GRANARY_SRC_DIR)/%.cc
	@echo "Building CXX object $@"
	@mkdir -p $(@D)
	@$(GRANARY_CXX) -Weverything $(GRANARY_CXX_FLAGS) -c $< -o $@

# Compile C files to object files.
$(GRANARY_BIN_DIR)/%.c.o :: $(GRANARY_SRC_DIR)/%.c
	@echo "Building C object $@"
	@mkdir -p $(@D)
	@$(GRANARY_CC) $(GRANARY_CC_FLAGS) -c $< -o $@

# Compile assembly files to object files.
$(GRANARY_BIN_DIR)/%.S.o :: $(GRANARY_SRC_DIR)/%.S
	@echo "Building ASM object $@"
	@mkdir -p $(@D)
	@$(GRANARY_CC) $(GRANARY_COMMON_FLAGS) $(GRANARY_ARCH_FLAGS) -c $< -o $@

# Build the Granary executable.
$(GRANARY_BIN_DIR)/grrplay: $(PLAY_OBJECT_FILES)
	@echo "Linking $@"
	@$(GRANARY_CXX) \
		$(GRANARY_CXX_FLAGS) \
		-o $@ \
		$^ \
		$(GRANARY_LIB_DIR)/xed-intel64/lib/libxed.a \
		-lgflags \
		-lpthread \

# Build the Granary executable.
$(GRANARY_BIN_DIR)/grrshot: $(SNAPSHOT_OBJ_FILES)
	@echo "Linking $@"
	@$(GRANARY_CXX) \
		$(GRANARY_CXX_FLAGS) \
		-o $@ \
		$^ \
		-lpthread \
		-lgflags

# Build the program to print out a code cache.
$(GRANARY_BIN_DIR)/grrcov: $(DUMP_OBJECT_FILES)
	@echo "Linking $@"
	@$(GRANARY_CXX) \
		$(GRANARY_CXX_FLAGS) \
		-o $@ \
		$^ \
		-lpthread \
		-lgflags

clean:
	@rm -rf $(GRANARY_BIN_DIR)

all: $(GRANARY_BIN_DIR)/grrplay $(GRANARY_BIN_DIR)/grrshot $(GRANARY_BIN_DIR)/grrcov
	@echo "Done."

install:
	mkdir -p $(GRANARY_PREFIX_DIR)/bin
	cp $(GRANARY_BIN_DIR)/grrplay $(GRANARY_PREFIX_DIR)/bin
	cp $(GRANARY_BIN_DIR)/grrshot $(GRANARY_PREFIX_DIR)/bin
	cp $(GRANARY_BIN_DIR)/grrcov $(GRANARY_PREFIX_DIR)/bin
