//
//  m3_info.h
//
//  Created by Steven Massey on 12/6/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#ifndef m3_info_h
#define m3_info_h

#include "m3_compile.h"

#if d_m3LogOutput

void            dump_type_stack         (IM3Compilation o);
void            log_opcode              (IM3Compilation o, u8 i_opcode);
const char *    get_indention_string    (IM3Compilation o);
void            emit_stack_dump         (IM3Compilation o);
void            log_emit                (IM3Compilation o, IM3Operation i_operation);

#else // d_m3LogOutput

#define         dump_type_stack(...)      {}
#define         log_opcode(...)           {}
#define         get_indention_string(...) ""
#define         emit_stack_dump(...)      {}
#define         log_emit(...)             {}

#endif // d_m3LogOutput

#endif // m3_info_h
