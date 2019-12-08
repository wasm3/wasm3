//
//  m3_info.h
//  m3
//
//  Created by Steven Massey on 12/6/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#ifndef m3_info_h
#define m3_info_h

#include "m3_compile.h"


typedef struct OpInfo
{
    IM3OpInfo   info;
    u8          opcode;
}
OpInfo;


OpInfo FindOperationInfo  (IM3Operation i_operation);


void  dump_type_stack  (IM3Compilation o);


#endif /* m3_info_h */
