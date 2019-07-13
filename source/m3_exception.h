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

#define _(TRY)			{ result = TRY; if (result) goto catch; }
#define throw(ERROR)	{ result = ERROR; if (result) goto catch; }

#endif /* m3_exception_h */
