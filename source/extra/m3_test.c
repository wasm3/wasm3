//
//  m3_test.c
//  m3
//
//  Created by Steven Massey on 2/27/20.
//  Copyright © 2020 Steven Massey. All rights reserved.
//

#include <stdio.h>

//#include "m3_ext.h"
#include "m3_bind.h"

#define Test(NAME) printf ("test: %s\n", #NAME);
#define expect(TEST) if (not (TEST)) { printf ("failed: (%s) on line: %d\n", #TEST, __LINE__); }

int  main  (int i_argc, const char  * i_argv [])
{
    Test (signatures)
    {
        M3Result result;
        
        M3FuncType ftype = { NULL, 666, 255 };
        
        result = SignatureToFuncType (& ftype, "");                     expect (result == m3Err_funcSignatureMissingReturnType)
        
        result = SignatureToFuncType (& ftype, "()");                   expect (result == m3Err_funcSignatureMissingReturnType)

        result = SignatureToFuncType (& ftype, " v () ");               expect (result == m3Err_none)
                                                                        expect (ftype.returnType == c_m3Type_none)
                                                                        expect (ftype.numArgs == 0)
        
        
        result = SignatureToFuncType (& ftype, "f(IiF");                expect (result == m3Err_none)
                                                                        expect (ftype.returnType == c_m3Type_f32)
                                                                        expect (ftype.numArgs == 3)
        
        M3FuncType ftype2;
        result = SignatureToFuncType (& ftype2, "f(I i F)");            expect (result == m3Err_none);
                                                                        expect (AreFuncTypesEqual (& ftype, &ftype2));
    }
    
    return 0;
}
