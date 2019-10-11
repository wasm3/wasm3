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
#define EXC_PRINT       //printf("Exc: %s:%d\n", __FILE__, __LINE__);

#define _try
#define _(TRY)			{ result = TRY; if (result) { EXC_PRINT; goto _catch; } }
#define _throw(ERROR)	{ result = ERROR; EXC_PRINT; goto _catch; }

#endif /* m3_exception_h */
