//
//  m3_exception.h
//  m3
//
//  Created by Steven Massey on 7/5/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#ifndef m3_exception_h
#define m3_exception_h

// some macros to emulate try/catch
#define EXC_STR(X)      #X
#define EXC_TOSTR(x)    EXC_STR(x)
#define EXC_PRINT       //puts("Exc: " __FILE__ ":" EXC_TOSTR(__LINE__) "\n");

#define _try
#define _(TRY)          { result = TRY; if (result) { EXC_PRINT; goto _catch; } }
#define _throw(ERROR)   { result = ERROR; EXC_PRINT; goto _catch; }

#endif /* m3_exception_h */
