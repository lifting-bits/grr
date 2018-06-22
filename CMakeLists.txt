# largely lifted/referenced from
# https://github.com/trailofbits/remill/blob/master/CMakeLists.txt
project(grr)
cmake_minimum_required (VERSION 3.2)

set(GRR_SOURCE_DIR "${PROJECT_SOURCE_DIR}")

# what is the purpose of enable_language()?
# acc to https://stackoverflow.com/a/24536108
	# the language given to the LANGUAGE field of the project command or the
	# enable_language command is just a string, which will then be used by CMake
	# together with the language dependent variables to setup the build system

# first: assuming all dependencies are installed, build grr.

#
# compiler selection
#

set(CMAKE_C_COMPILER "clang-3.8")
set(CMAKE_CXX_COMPILER "clang++-3.8")


