//
//  m3_exception.h
//
//  Created by Steven Massey on 7/5/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#ifndef m3_exception_h
#define m3_exception_h

#include "m3_config.h"

// some macros to emulate try/catch
#define EXCEPTION_PRINT //puts("Exc: " __FILE__ ":" M3_STR(__LINE__) "\n");

#define _try
#define _(TRY)          { result = TRY; if (result) { EXCEPTION_PRINT; goto _catch; } }
#define _throw(ERROR)   { result = ERROR; EXCEPTION_PRINT; goto _catch; }

#endif // m3_exception_h
