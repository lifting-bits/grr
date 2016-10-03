/*BEGIN_LEGAL 
Copyright (c) 2004-2015, Intel Corporation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
/*
/// @file xed-interface.h 
/// 
*/



#if !defined(_XED_INTERFACE_H_)
# define _XED_INTERFACE_H_

#if defined(_WIN32) && defined(_MANAGED)
#pragma unmanaged
#endif
    
#include "../../../third_party/xed-intel64/include/xed-build-defines.h" /* generated */
    
#include "../../../third_party/xed-intel64/include/xed-common-hdrs.h"
#include "../../../third_party/xed-intel64/include/xed-types.h"
#include "../../../third_party/xed-intel64/include/xed-operand-enum.h"

#include "../../../third_party/xed-intel64/include/xed-init.h"
#include "../../../third_party/xed-intel64/include/xed-decode.h"
#include "../../../third_party/xed-intel64/include/xed-ild.h"

#include "../../../third_party/xed-intel64/include/xed-state.h" /* dstate, legacy */
#include "../../../third_party/xed-intel64/include/xed-syntax-enum.h"
#include "../../../third_party/xed-intel64/include/xed-reg-class-enum.h" /* generated */
#include "../../../third_party/xed-intel64/include/xed-reg-class.h"

#if defined(XED_ENCODER)
# include "../../../third_party/xed-intel64/include/xed-encode.h"
# include "../../../third_party/xed-intel64/include/xed-encoder-hl.h"
#endif
#include "../../../third_party/xed-intel64/include/xed-util.h"
#include "../../../third_party/xed-intel64/include/xed-operand-action.h"

#include "../../../third_party/xed-intel64/include/xed-version.h"
#include "../../../third_party/xed-intel64/include/xed-decoded-inst.h"
#include "../../../third_party/xed-intel64/include/xed-decoded-inst-api.h"
#include "../../../third_party/xed-intel64/include/xed-inst.h"
#include "../../../third_party/xed-intel64/include/xed-iclass-enum.h"    /* generated */
#include "../../../third_party/xed-intel64/include/xed-category-enum.h"  /* generated */
#include "../../../third_party/xed-intel64/include/xed-extension-enum.h" /* generated */
#include "../../../third_party/xed-intel64/include/xed-attribute-enum.h" /* generated */
#include "../../../third_party/xed-intel64/include/xed-exception-enum.h" /* generated */
#include "../../../third_party/xed-intel64/include/xed-operand-element-type-enum.h"  /* generated */
#include "../../../third_party/xed-intel64/include/xed-operand-element-xtype-enum.h" /* generated */

#include "../../../third_party/xed-intel64/include/xed-disas.h"  // callbacks for disassembly
#include "../../../third_party/xed-intel64/include/xed-format-options.h" /* options for disassembly  */

#include "../../../third_party/xed-intel64/include/xed-iform-enum.h"     /* generated */
/* indicates the first and last index of each iform, for building tables */
#include "../../../third_party/xed-intel64/include/xed-iformfl-enum.h"   /* generated */
/* mapping iforms to iclass/category/extension */
#include "../../../third_party/xed-intel64/include/xed-iform-map.h"
#include "../../../third_party/xed-intel64/include/xed-rep-prefix.h"  


#include "../../../third_party/xed-intel64/include/xed-agen.h"
#include "../../../third_party/xed-intel64/include/xed-cpuid-rec.h"  


#endif
