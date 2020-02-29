//
//  m3_test.c
//
//  Created by Steven Massey on 2/27/20.
//  Copyright © 2020 Steven Massey. All rights reserved.
//

#include <stdio.h>

//#include "m3_ext.h"
#include "m3_bind.h"

#define Test(NAME) printf ("\n  test: %s\n", #NAME);
#define expect(TEST) if (not (TEST)) { printf ("failed: (%s) on line: %d\n", #TEST, __LINE__); }

int  main  (int i_argc, const char  * i_argv [])
{
    Test (signatures)
    {
        M3Result result;
        
        IM3FuncType ftype = NULL;
        
        result = SignatureToFuncType (& ftype, "");                     expect (result == m3Err_malformedFunctionSignature)
        m3Free (ftype);
        
        result = SignatureToFuncType (& ftype, "()");                   expect (result == m3Err_malformedFunctionSignature)
        m3Free (ftype);

        result = SignatureToFuncType (& ftype, " v () ");               expect (result == m3Err_none)
                                                                        expect (ftype->returnType == c_m3Type_none)
                                                                        expect (ftype->numArgs == 0)
        m3Free (ftype);

        result = SignatureToFuncType (& ftype, "f(IiF)");               expect (result == m3Err_none)
                                                                        expect (ftype->returnType == c_m3Type_f32)
                                                                        expect (ftype->numArgs == 3)
                                                                        expect (ftype->argTypes [0] == c_m3Type_i64)
                                                                        expect (ftype->argTypes [1] == c_m3Type_i32)
                                                                        expect (ftype->argTypes [2] == c_m3Type_f64)
        
        IM3FuncType ftype2 = NULL;
        result = SignatureToFuncType (& ftype2, "f(I i F)");            expect (result == m3Err_none);
                                                                        expect (AreFuncTypesEqual (ftype, ftype2));
        m3Free (ftype);
        m3Free (ftype2);
    }
    
    return 0;
}
