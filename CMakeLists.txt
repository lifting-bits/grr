# largely lifted/referenced from
# https://github.com/trailofbits/remill/blob/master/CMakeLists.txt
project(grr)
cmake_minimum_required (VERSION 3.2)

# what is the purpose of enable_language()?
# acc to https://stackoverflow.com/a/24536108
	# the language given to the LANGUAGE field of the project command or the
	# enable_language command is just a string, which will then be used by CMake
	# together with the language dependent variables to setup the build system
enable_language(C)
enable_language(CXX)
enable_language(ASM)

# first: assuming all dependencies are installed, build grr.

# What compilers are we using?
set(CMAKE_C_COMPILER "clang-3.8")
set(CMAKE_CXX_COMPILER "clang++-3.8")

# Where is Granary's source code located?
set(GRANARY_SRC_DIR "${PROJECT_SOURCE_DIR}")
set(GRANARY_LIB_DIR  "${GRANARY_SRC_DIR}/third_party")
set(GRANARY_GEN_DIR  "${GRANARY_SRC_DIR}/gen")

# What OS are we compiling for?
set(GRANARY_OS "decree")

# Where will Granary run? `kernel` or `user` space.
set(GRANARY_WHERE "user")

# What type of build should be perform?
# TODO: Use CMAKE_BUILD_TYPE
set(GRANARY_TARGET "debug")

# Useful for distinguishing different kinds of builds.
set(GRANARY_TRIPLE "${GRANARY_TARGET}_${GRANARY_OS}_${GRANARY_WHERE}")

# Where should we emit object files and the executable?
set(GRANARY_BIN_DIR "${GRANARY_SRC_DIR}/build/${GRANARY_TRIPLE}")

# Should we assume that Granary will be executed with Valgrind?
set(GRANARY_WITH_VALGRIND 0)

# Compiler warnings that are explicitly disabled.
# Alternatively, have these as a target_compile_options instead.
set(GRANARY_DISABLED_WARNINGS "-Wno-gnu-anonymous-struct ")
list(APPEND GRANARY_DISABLED_WARNINGS "-Wno-gnu-conditional-omitted-operand ")
list(APPEND GRANARY_DISABLED_WARNINGS "-Wno-long-long ")
list(APPEND GRANARY_DISABLED_WARNINGS "-Wno-gnu-statement-expression ")
list(APPEND GRANARY_DISABLED_WARNINGS "-Wno-nested-anon-types ")
list(APPEND GRANARY_DISABLED_WARNINGS "-Wno-extended-offsetof ")
list(APPEND GRANARY_DISABLED_WARNINGS "-Wno-c++98-compat-pedantic -Wno-c++98-compat ")
list(APPEND GRANARY_DISABLED_WARNINGS "-Wno-padded ")
list(APPEND GRANARY_DISABLED_WARNINGS "-Wno-unused-macros ")
list(APPEND GRANARY_DISABLED_WARNINGS "-Wno-missing-variable-declarations ")
list(APPEND GRANARY_DISABLED_WARNINGS "-Wno-missing-prototypes ")
list(APPEND GRANARY_DISABLED_WARNINGS "-Wno-packed ")
list(APPEND GRANARY_DISABLED_WARNINGS "-Wno-global-constructors ")
list(APPEND GRANARY_DISABLED_WARNINGS "-Wno-exit-time-destructors ")
list(APPEND GRANARY_DISABLED_WARNINGS "-Wno-disabled-macro-expansion ")
list(APPEND GRANARY_DISABLED_WARNINGS "-Wno-date-time ")
list(APPEND GRANARY_DISABLED_WARNINGS "-Wno-reserved-id-macro ")

# Arch-specific flags.
set(GRANARY_ARCH_FLAGS "-m64 -mtune=native -fPIC -ffreestanding ")
list(APPEND GRANARY_ARCH_FLAGS "-ftls-model=initial-exec -mno-red-zone ")

# Flags that are common to both C and C++ compilers.
list(APPEND GRANARY_COMMON_FLAGS "-I${GRANARY_SRC_DIR} ")
list(APPEND GRANARY_COMMON_FLAGS "-Wall -Werror -Wpedantic  ")
list(APPEND GRANARY_COMMON_FLAGS "${GRANARY_DISABLED_WARNINGS} ")
list(APPEND GRANARY_COMMON_FLAGS "-DGRANARY_WHERE_${GRANARY_WHERE} ")
list(APPEND GRANARY_COMMON_FLAGS "-DGRANARY_OS_${GRANARY_OS} ")
list(APPEND GRANARY_COMMON_FLAGS "-DGRANARY_TARGET_${GRANARY_TARGET} ")
list(APPEND GRANARY_COMMON_FLAGS "-DGOOGLE_PROTOBUF_NO_RTTI ")

set(GRANARY_SANITIZER "")

# Optimization and debug information level.
# TODO: CMAKE_BUILD_TYPE instead
if ("debug" EQUAL ${GRANARY_TARGET})
	list(APPEND GRANARY_COMMON_FLAGS "-O0 -g3 -fno-inline ")
	if (defined ${GRANARY_SANITIZER})
		list(APPEND GRANARY_COMMON_FLAGS "-fsanitize=${GRANARY_SANITIZER} ")
	endif ()
else ()
	list(APPEND GRANARY_COMMON_FLAGS "-Oz -g3 ")
endif ()

# Flags to pass to the various compilers.
set(GRANARY_CC_FLAGS "-std=c11 ${GRANARY_COMMON_FLAGS} ${GRANARY_ARCH_FLAGS} ")
set(GRANARY_CXX_FLAGS "-std=c++11")
list(APPEND GRANARY_CXX_FLAGS "${GRANARY_COMMON_FLAGS} ${GRANARY_ARCH_FLAGS} ")
list(APPEND GRANARY_CXX_FLAGS "-fno-exceptions -fno-asynchronous-unwind-tables -fno-rtti ")
list(APPEND GRANARY_CXX_FLAGS "-isystem ${GRANARY_LIB_DIR}/gflags/include ")

# # C, C++, and assembly files in Granary.
set(GRANARY_SRC_FILES
	"./granary/input/record.cc"
	"./granary/input/mutate.cc"
	"./granary/base/breakpoint.cc"
	"./granary/base/interrupt.cc"
	"./granary/os/schedule.cc"
	"./granary/os/process.cc"
	"./granary/os/decree_user/syscall.cc"
	"./granary/os/decree_user/snapshot.cc"
	"./granary/os/snapshot.cc"
	"./granary/os/file.cc"
	"./granary/code/instruction.cc"
	"./granary/code/instrument.cc"
	"./granary/code/index.cc"
	"./granary/code/block.cc"
	"./granary/code/cache.cc"
	"./granary/code/coverage.cc"
	"./granary/code/execute.cc"
	"./granary/code/trace.cc"
	"./granary/code/branch_tracer.cc"
	"./granary/arch/x86/instruction.cc"
	"./granary/arch/x86/cpu.cc"
	"./granary/arch/x86/instrument.cc"
	"./granary/arch/x86/patch.cc"
	"./granary/arch/x86/process.cc"
	"./granary/arch/x86/branch_tracer.S"
	"./granary/arch/x86/block.cc"
	"./granary/arch/x86/fault.cc"
	"./granary/arch/x86/base.cc"
	"./granary/arch/x86/coverage.S"
	"./granary/arch/x86/cache.S"
	"./granary/arch/x86/syscall.S"
	"./granary/arch/x86/trace.cc"
	)

list(APPEND GRANARY_SRC_FILES
	"./third_party/radamsa/radamsa.c"
	"./third_party/radamsa/radamsa.cc"
	"./third_party/md5/md5.cc"
	"./third_party/xxhash/xxhash.c"
	)

set(DUMP_SRC_FILES
	"/coverage.cc"
    "/granary/code/index.cc"
    "/granary/base/breakpoint.cc"
    "/granary/base/interrupt.cc"
    "/xxhash/xxhash.c"
	)

set(PLAY_SRC_FILES
	"${GRANARY_SRC_DIR}/play.cc"
	"${GRANARY_SRC_FILES}"
	)

set(SNAPSHOT_SRC_FILES
	"${GRANARY_SRC_DIR}/snapshot.cc"
	"${GRANARY_SRC_DIR}/granary/os/snapshot.cc"
	"${GRANARY_SRC_DIR}/granary/os/decree_user/snapshot.cc"
	"${GRANARY_SRC_DIR}/granary/base/breakpoint.cc"
	"${GRANARY_SRC_DIR}/granary/base/interrupt.cc"
)

# build the actual executables
add_executable(grrplay ${PLAY_SRC_FILES})
target_link_libraries(grrplay gflags pthread)
# add_executable(grrshot ${SNAPSHOT_SRC_FILES})
# add_executable(grrcov ${DUMP_SRC_FILES})

# make install
