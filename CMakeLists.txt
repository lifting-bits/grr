# largely lifted/referenced from
# https://github.com/trailofbits/remill/blob/master/CMakeLists.txt
project(grr)
cmake_minimum_required (VERSION 3.2)

enable_language(C)
enable_language(CXX)
enable_language(ASM)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	# pass
else() # using anything else, including AppleClang
	message(FATAL_ERROR "Please use clang as your C compiler\n-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++")
endif()

if (DEFINED ENV{TRAILOFBITS_LIBRARIES})
    set(LIBRARY_REPOSITORY_ROOT $ENV{TRAILOFBITS_LIBRARIES}
        CACHE PATH "Location of cxx-common libraries.")
endif ()

if (DEFINED LIBRARY_REPOSITORY_ROOT)
    set(TOB_CMAKE_INCLUDE "${LIBRARY_REPOSITORY_ROOT}/cmake_modules/repository.cmake")
    set(LEGACY_TOB_CMAKE_INCLUDE "${LIBRARY_REPOSITORY_ROOT}/cmake/repository.cmake")

    if (EXISTS "${LEGACY_TOB_CMAKE_INCLUDE}")
        include("${LEGACY_TOB_CMAKE_INCLUDE}")
        message(WARNING "Using legacy cxx-common build; please update!")
    elseif (EXISTS "${TOB_CMAKE_INCLUDE}")
        include("${TOB_CMAKE_INCLUDE}")

    else ()
        message(FATAL_ERROR "The library repository could not be found!")
    endif ()

    message(STATUS "Using the following library repository: ${LIBRARY_REPOSITORY_ROOT}")
else ()
    message(STATUS "Using system libraries")
endif ()

# Where is Granary's source code located?
set(GRANARY_SRC_DIR "${PROJECT_SOURCE_DIR}")
set(GRANARY_LIB_DIR  "${GRANARY_SRC_DIR}/third_party")

# What OS are we compiling for?
set(GRANARY_OS "decree")

# Where will Granary run? `kernel` or `user` space.
set(GRANARY_WHERE "user")

# Useful for distinguishing different kinds of builds.
set(GRANARY_TRIPLE "${CMAKE_BUILD_TYPE}_${GRANARY_OS}_${GRANARY_WHERE}")

# Where should we emit object files and the executable?
set(GRANARY_BIN_DIR "${GRANARY_SRC_DIR}/build/${GRANARY_TRIPLE}")
# This is currently unused, since CMake should automatically handle this if you're using it with an IDE.

# Should we assume that Granary will be executed with Valgrind?
set(GRANARY_WITH_VALGRIND 0)

# Compiler warnings that are explicitly disabled.
# Alternatively, have these as a target_compile_options instead.
set(GRANARY_DISABLED_WARNINGS "-Wno-gnu-anonymous-struct")
set(GRANARY_DISABLED_WARNINGS "${GRANARY_DISABLED_WARNINGS} -Wno-gnu-conditional-omitted-operand")
set(GRANARY_DISABLED_WARNINGS "${GRANARY_DISABLED_WARNINGS} -Wno-long-long")
set(GRANARY_DISABLED_WARNINGS "${GRANARY_DISABLED_WARNINGS} -Wno-gnu-statement-expression")
set(GRANARY_DISABLED_WARNINGS "${GRANARY_DISABLED_WARNINGS} -Wno-nested-anon-types")
set(GRANARY_DISABLED_WARNINGS "${GRANARY_DISABLED_WARNINGS} -Wno-extended-offsetof")
set(GRANARY_DISABLED_WARNINGS "${GRANARY_DISABLED_WARNINGS} -Wno-c++98-compat-pedantic -Wno-c++98-compat")
set(GRANARY_DISABLED_WARNINGS "${GRANARY_DISABLED_WARNINGS} -Wno-padded")
set(GRANARY_DISABLED_WARNINGS "${GRANARY_DISABLED_WARNINGS} -Wno-unused-macros")
set(GRANARY_DISABLED_WARNINGS "${GRANARY_DISABLED_WARNINGS} -Wno-missing-variable-declarations")
set(GRANARY_DISABLED_WARNINGS "${GRANARY_DISABLED_WARNINGS} -Wno-missing-prototypes")
set(GRANARY_DISABLED_WARNINGS "${GRANARY_DISABLED_WARNINGS} -Wno-packed")
set(GRANARY_DISABLED_WARNINGS "${GRANARY_DISABLED_WARNINGS} -Wno-global-constructors")
set(GRANARY_DISABLED_WARNINGS "${GRANARY_DISABLED_WARNINGS} -Wno-exit-time-destructors")
set(GRANARY_DISABLED_WARNINGS "${GRANARY_DISABLED_WARNINGS} -Wno-disabled-macro-expansion")
set(GRANARY_DISABLED_WARNINGS "${GRANARY_DISABLED_WARNINGS} -Wno-date-time")
set(GRANARY_DISABLED_WARNINGS "${GRANARY_DISABLED_WARNINGS} -Wno-reserved-id-macro")

# Arch-specific flags.
set(GRANARY_ARCH_FLAGS "-m64 -mtune=native -fPIC -ffreestanding ")
set(GRANARY_ARCH_FLAGS "${GRANARY_ARCH_FLAGS} -ftls-model=initial-exec -mno-red-zone")

# Flags that are common to both C and C++ compilers.
set(GRANARY_COMMON_FLAGS "${GRANARY_COMMON_FLAGS} -I${GRANARY_SRC_DIR}")
set(GRANARY_COMMON_FLAGS "${GRANARY_COMMON_FLAGS} -Wall -Wpedantic ")
set(GRANARY_COMMON_FLAGS "${GRANARY_COMMON_FLAGS} ${GRANARY_DISABLED_WARNINGS}")
set(GRANARY_COMMON_FLAGS "${GRANARY_COMMON_FLAGS} -DGRANARY_WHERE_${GRANARY_WHERE}")
set(GRANARY_COMMON_FLAGS "${GRANARY_COMMON_FLAGS} -DGRANARY_OS_${GRANARY_OS}")
set(GRANARY_COMMON_FLAGS "${GRANARY_COMMON_FLAGS} -DGRANARY_TARGET_${CMAKE_BUILD_TYPE}")

# Optimization and debug information level.
if (${CMAKE_BUILD_TYPE} MATCHES "DEBUG" )
  set(GRANARY_COMMON_FLAGS "${GRANARY_COMMON_FLAGS} -O0 -g3 -fno-inline")
	if (GRANARY_SANITIZER)
		set(GRANARY_COMMON_FLAGS "${GRANARY_COMMON_FLAGS} -fsanitize=${GRANARY_SANITIZER}")
	endif ()
else ()
	set(GRANARY_COMMON_FLAGS "${GRANARY_COMMON_FLAGS} -Oz -g3")
endif ()

# Flags to pass to the various compilers.
set(CMAKE_C_FLAGS "-std=c11 ${GRANARY_COMMON_FLAGS} ${GRANARY_ARCH_FLAGS}")
set(CMAKE_CXX_FLAGS "-std=c++11")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${GRANARY_COMMON_FLAGS} ${GRANARY_ARCH_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-asynchronous-unwind-tables -fno-rtti")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -isystem ${GRANARY_LIB_DIR}/gflags/include")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Weverything")
set(CMAKE_ASM_FLAGS "${GRANARY_COMMON_FLAGS} ${GRANARY_ARCH_FLAGS}")

# dependencies
# xed
find_package(XED REQUIRED)
list(APPEND PROJECT_LIBRARIES ${XED_LIBRARIES})
list(APPEND PROJECT_INCLUDEDIRECTORIES ${XED_INCLUDE_DIRS})

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
	"./granary/arch/x86/block.cc"
	"./granary/arch/x86/fault.cc"
	"./granary/arch/x86/base.cc"
	"./granary/arch/x86/trace.cc"
	"./granary/arch/x86/branch_tracer.S"
	"./granary/arch/x86/coverage.S"
	"./granary/arch/x86/cache.S"
	"./granary/arch/x86/syscall.S"
	)

list(APPEND GRANARY_SRC_FILES
	"./third_party/radamsa/radamsa.c"
	"./third_party/radamsa/radamsa.cc"
	"./third_party/md5/md5.cc"
	"./third_party/xxhash/xxhash.c"
	)

set(DUMP_SRC_FILES
	"./coverage.cc"
    "./granary/code/index.cc"
    "./granary/base/breakpoint.cc"
    "./granary/base/interrupt.cc"
    "./third_party/xxhash/xxhash.c"
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

# Build the actual executables
add_executable(grrplay ${PLAY_SRC_FILES})
target_include_directories(grrplay PUBLIC ${GRANARY_SRC_DIR} ${PROJECT_INCLUDEDIRECTORIES})
target_link_libraries(grrplay gflags pthread ${PROJECT_LIBRARIES})

add_executable(grrshot ${SNAPSHOT_SRC_FILES})
target_link_libraries(grrshot gflags pthread)

add_executable(grrcov ${DUMP_SRC_FILES})
target_link_libraries(grrcov gflags pthread)

install(TARGETS grrplay grrshot grrcov
		DESTINATION "${GRANARY_PREFIX_DIR}/bin"
		PERMISSIONS OWNER_READ OWNER_EXECUTE
					GROUP_READ GROUP_EXECUTE
					WORLD_READ WORLD_EXECUTE
)
