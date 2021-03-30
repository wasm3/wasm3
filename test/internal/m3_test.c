//
//  m3_test.c
//
//  Created by Steven Massey on 2/27/20.
//  Copyright © 2020 Steven Massey. All rights reserved.
//

#include <stdio.h>

#include "wasm3_ext.h"
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
        m3_Free (ftype);
        
          // implicit void return
        result = SignatureToFuncType (& ftype, "()");                   expect (result == m3Err_none)
        m3_Free (ftype);

        result = SignatureToFuncType (& ftype, " v () ");               expect (result == m3Err_none)
                                                                        expect (ftype->numRets == 0)
                                                                        expect (ftype->numArgs == 0)
        m3_Free (ftype);

        result = SignatureToFuncType (& ftype, "f(IiF)");               expect (result == m3Err_none)
                                                                        expect (ftype->numRets == 1)
                                                                        expect (ftype->types [0] == c_m3Type_f32)
                                                                        expect (ftype->numArgs == 3)
                                                                        expect (ftype->types [1] == c_m3Type_i64)
                                                                        expect (ftype->types [2] == c_m3Type_i32)
                                                                        expect (ftype->types [3] == c_m3Type_f64)
        
        IM3FuncType ftype2 = NULL;
        result = SignatureToFuncType (& ftype2, "f(I i F)");            expect (result == m3Err_none);
                                                                        expect (AreFuncTypesEqual (ftype, ftype2));
        m3_Free (ftype);
        m3_Free (ftype2);
    }
    
    
    Test (codepages.simple)
    {
        M3Environment env = { 0 };
        M3Runtime runtime = { 0 };
        runtime.environment = & env;
        
        IM3CodePage page = AcquireCodePage (& runtime);                 expect (page);
                                                                        expect (runtime.numCodePages == 1);
                                                                        expect (runtime.numActiveCodePages == 1);
        
        IM3CodePage page2 = AcquireCodePage (& runtime);                expect (page2);
                                                                        expect (runtime.numCodePages == 2);
                                                                        expect (runtime.numActiveCodePages == 2);

        ReleaseCodePage (& runtime, page);                              expect (runtime.numCodePages == 2);
                                                                        expect (runtime.numActiveCodePages == 1);

        ReleaseCodePage (& runtime, page2);                             expect (runtime.numCodePages == 2);
                                                                        expect (runtime.numActiveCodePages == 0);
        
        Runtime_Release (& runtime);                                    expect (CountCodePages (env.pagesReleased) == 2);
        Environment_Release (& env);                                    expect (CountCodePages (env.pagesReleased) == 0);
    }
    
    
    Test (codepages.b)
    {
        const u32 c_numPages = 2000;
        IM3CodePage pages [2000] = { NULL };
        
        M3Environment env = { 0 };
        M3Runtime runtime = { 0 };
        runtime.environment = & env;

        u32 numActive = 0;
        
        for (u32 i = 0; i < 2000000; ++i)
        {
            u32 index = rand () % c_numPages;   // printf ("%5u ", index);
            
            if (pages [index] == NULL)
            {
//                printf ("acq\n");
                pages [index] = AcquireCodePage (& runtime);
                ++numActive;
            }
            else
            {
//                printf ("rel\n");
                ReleaseCodePage (& runtime, pages [index]);
                pages [index] = NULL;
                --numActive;
            }
                
            expect (runtime.numActiveCodePages == numActive);
        }
          
        printf ("num pages: %d\n", runtime.numCodePages);
        
        for (u32 i = 0; i < c_numPages; ++i)
        {
            if (pages [i])
            {
                ReleaseCodePage (& runtime, pages [i]);
                pages [i] = NULL;
                --numActive;                                            expect (runtime.numActiveCodePages == numActive);
            }
        }
        
        Runtime_Release (& runtime);
        Environment_Release (& env);
    }
     
     
    Test (extensions)
    {
        IM3Environment env = m3_NewEnvironment ();

        IM3Runtime runtime = m3_NewRuntime (env, 1024, NULL);

        IM3Module module = m3_NewModule (env);

        M3Result result = m3_LoadModule (runtime, module);                    expect (result == m3Err_none)

        m3_FreeRuntime (runtime);
    }
    
    
    return 0;
}
