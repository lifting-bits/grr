/* Copyright 2019 Peter Goodman, all rights reserved. */

#pragma once

#ifdef __APPLE__
# define SYMBOL(x) _ ## x
#else
# define SYMBOL(x) x
#endif

    .intel_syntax noprefix

#ifdef __APPLE__
# define TEXT_SECTION .section __TEXT,__text
# define DATA_SECTION .section __DATA,__data
# define CONST_SECTION .section __TEXT,__const
#else
# define TEXT_SECTION .text
# define DATA_SECTION .data
# define CONST_SECTION .rodata
#endif
