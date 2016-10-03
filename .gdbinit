set logging off
set breakpoint pending on
set print demangle on
set print asm-demangle on
set print object on
set print static-members on
set disassembly-flavor intel
set backtrace limit 20
set language c++

b granary_curiosity
b granary_unreachable

