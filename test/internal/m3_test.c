//
//  m3_test.c
//
//  Created by Steven Massey on 2/27/20.
//  Copyright © 2020 Steven Massey. All rights reserved.
//

#include <stdio.h>

#include "wasm3_ext.h"
#include "m3_bind.h"

#define Test(NAME) if (RunTest (argc, argv, #NAME) != 0)
#define DisabledTest(NAME) printf ("\ndisabled: %s\n", #NAME); if (false)
#define expect(TEST) if (not (TEST)) { printf ("failed: (%s) on line: %d\n", #TEST, __LINE__); }


bool RunTest (int i_argc, const char * i_argv [], cstr_t i_name)
{
	cstr_t option = (i_argc == 2) ? i_argv [1] : NULL;
	
	bool runningTest = option ? strcmp (option, i_name) == 0 : true;
	
	if (runningTest)
		printf ("\n    test: %s\n", i_name);
	
	return runningTest;
}


int  main  (int argc, const char  * argv [])
{
//	m3_PrintM3Info ();
	
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
        M3Result result;
        
        IM3Environment env = m3_NewEnvironment ();

        IM3Runtime runtime = m3_NewRuntime (env, 1024, NULL);

        IM3Module module = w3x_NewModule (env);

        
        i32 functionIndex = -1;
        
        u8 wasm [5] = { 0x04,       // size
                        0x00,       // num local defs
                        0x41, 0x37, // i32.const= 55
                        0x0b        // end block
        };
        
        // will partially fail (compilation) because module isn't attached to a runtime yet.
        result = w3x_InjectFunction (module, & functionIndex, "test", "i()", wasm, 5, true);    expect (result != m3Err_none)
                                                                                        expect (functionIndex >= 0)

        result = m3_LoadModule (runtime, module);                                       expect (result == m3Err_none)

        // try again
        result = w3x_InjectFunction (module, & functionIndex, "test", "i()", wasm, 5, true);    expect (result == m3Err_none)

        IM3Function function = m3_GetFunctionByIndex (module, functionIndex);           expect (function)
        
        if (function)
        {
            result = m3_CallV (function);                                               expect (result == m3Err_none)
            u32 ret = 0;
            m3_GetResultsV (function, & ret);                                           expect (ret == 55);
        }

        m3_FreeRuntime (runtime);
    }


    Test (generate.roundtrip)
    {
        M3Result result;

        IM3Environment genEnv = m3_NewEnvironment ();
        IM3Runtime runtime = m3_NewRuntime (genEnv, 8192, NULL);

#       if 0
        (module
            (memory (export "mem") 1)
            (func (result i32)              ;; func 0: NOT exported; only named via the name section
                i32.const 0
                i32.load8_u)
            (data (i32.const 0) "\ab")
            ;; custom "name" section names func 0 -> "reader"
        )
#       endif

        u8 wasm [71] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,                 // header
            0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f,                       // Type:   () -> i32
            0x03, 0x02, 0x01, 0x00,                                         // Func:   one function of type 0
            0x05, 0x03, 0x01, 0x00, 0x01,                                   // Memory: min 1 page
            0x07, 0x07, 0x01, 0x03, 0x6d, 0x65, 0x6d, 0x02, 0x00,           // Export: "mem" memory 0
            0x0a, 0x09, 0x01, 0x07, 0x00, 0x41, 0x00, 0x2d, 0x00, 0x00, 0x0b, // Code:  i32.const 0; i32.load8_u; end
            0x0b, 0x07, 0x01, 0x00, 0x41, 0x00, 0x0b, 0x01, 0xab,          // Data:   offset 0 = { 0xAB }
            0x00, 0x10, 0x04, 0x6e, 0x61, 0x6d, 0x65,                       // Custom "name",
                  0x01, 0x09, 0x01, 0x00, 0x06, 0x72, 0x65, 0x61, 0x64, 0x65, 0x72  //  funcnames: { 0 -> "reader" }
        };

        IM3Module module = NULL;
        result = m3_ParseModule (genEnv, & module, wasm, 71, true);                  expect (result == m3Err_none)
        // reserve a slot so a function can be injected after the module is loaded
        result = w3x_ReserveFunctions (module, 1);                                   expect (result == m3Err_none)
        result = m3_LoadModule (runtime, module);                                   expect (result == m3Err_none)

        // func 0 picked up its name from the custom name section
        IM3Function reader = m3_GetFunctionByIndex (module, 0);                      expect (reader)
        expect (reader and strcmp (m3_GetFunctionName (reader), "reader") == 0)

        // data segment initialized memory[0] to 0xAB (read via func 0, by index)
        result = m3_CompileModule (module);                                         expect (result == m3Err_none)
        if (reader)
        {
            result = m3_CallV (reader);                                             expect (result == m3Err_none)
            u32 v = 0; m3_GetResultsV (reader, & v);                                expect (v == 0xAB)
        }

        // mutate the live linear memory -- this is what the snapshot must capture
        u32 memSize = 0;
        u8 * mem = m3_GetMemory (runtime, & memSize, 0);                            expect (mem and memSize)
        if (mem) mem [0] = 0x5C;

        // inject a brand new function (not present in the original wasm)
        i32 injectedIndex = -1;
        u8 injected [5] = { 0x04, 0x00, 0x41, 0x37, 0x0b };                          // size; no locals; i32.const 55; end
        result = w3x_InjectFunction (module, & injectedIndex, "inj", "i()", injected, 5, true);
                                                                                    expect (result == m3Err_none)
                                                                                    expect (injectedIndex == 1)

        // generate a wasm snapshot of the live module
        u8 * gen = NULL; u32 genLen = 0;
        result = w3x_GenerateWasmModule (module, & gen, & genLen);                   expect (result == m3Err_none)
                                                                                    expect (gen and genLen)
        // sanity: header
        expect (gen [0] == 0x00 and gen [1] == 0x61 and gen [2] == 0x73 and gen [3] == 0x6d)

        // re-parse / re-instantiate the snapshot into a fresh runtime
        IM3Runtime runtime2 = m3_NewRuntime (genEnv, 8192, NULL);
        IM3Module module2 = NULL;
        result = m3_ParseModule (genEnv, & module2, gen, genLen, true);             expect (result == m3Err_none)
        result = m3_LoadModule (runtime2, module2);                                 expect (result == m3Err_none)
        expect (w3x_GetNumFunctions (module2) == 2)

        // the custom name section round-tripped: func 0 is unexported, so "reader" can only have come
        // from the regenerated name section
        IM3Function reader2 = m3_GetFunctionByIndex (module2, 0);                    expect (reader2)
        expect (reader2 and strcmp (m3_GetFunctionName (reader2), "reader") == 0)

        // the injected function's name was captured too
        IM3Function inj2 = m3_GetFunctionByIndex (module2, 1);                       expect (inj2)
        expect (inj2 and strcmp (m3_GetFunctionName (inj2), "inj") == 0)

        result = m3_CompileModule (module2);                                        expect (result == m3Err_none)

        // data segment now reflects the mutated runtime memory (0x5C, not 0xAB)
        if (reader2)
        {
            result = m3_CallV (reader2);                                            expect (result == m3Err_none)
            u32 v = 0; m3_GetResultsV (reader2, & v);                               expect (v == 0x5C)
        }

        // the injected function survived the round-trip (captured from its function wasm)
        if (inj2)
        {
            result = m3_CallV (inj2);                                               expect (result == m3Err_none)
            u32 v = 0; m3_GetResultsV (inj2, & v);                                  expect (v == 55)
        }

        m3_Free (gen);
        m3_FreeRuntime (runtime2);
        m3_FreeRuntime (runtime);
        m3_FreeEnvironment (genEnv);
    }


    Test (generate.real)
    {
        M3Result result;

        // regenerates a real, feature-rich module (imports, table, globals, elements, multiple data
        // segments) and dumps it for external validation (wasm-validate / wasmtime).  Skips if absent.
        FILE * f = fopen ("/Users/smassey/Code/Gestalt/gestalt/build/Gestalt.wasm", "rb");
        if (not f)
            printf ("skipped: Gestalt.wasm not found\n");
        else
        {
            fseek (f, 0, SEEK_END); long len = ftell (f); fseek (f, 0, SEEK_SET);
            u8 * buf = (u8 *) malloc (len);
            fread (buf, 1, len, f); fclose (f);

            IM3Environment e = m3_NewEnvironment ();
            IM3Runtime r = m3_NewRuntime (e, 64 * 1024, NULL);

            IM3Module module = NULL;
            result = m3_ParseModule (e, & module, buf, (u32) len, true);            expect (result == m3Err_none)
            result = m3_LoadModule (r, module);                                     expect (result == m3Err_none)

            u8 * gen = NULL; u32 genLen = 0;
            result = w3x_GenerateWasmModule (module, & gen, & genLen);              expect (result == m3Err_none)
            if (not result)
            {
                FILE * o = fopen ("/tmp/gen_real.wasm", "wb");
                if (o) { fwrite (gen, 1, genLen, o); fclose (o); printf ("wrote /tmp/gen_real.wasm (%u bytes)\n", genLen); }
            }

            m3_Free (gen);
            m3_FreeRuntime (r);
            m3_FreeEnvironment (e);
            free (buf);
        }
    }


	IM3Environment env = m3_NewEnvironment ();


	Test (multireturn.a)
	{
		M3Result result;

        IM3Runtime runtime = m3_NewRuntime (env, 1024, NULL);

#		if 0
		(module
			(func (result i32 f32)

				i32.const 1234
				f32.const 5678.9
			)

			(export "main" (func 0))
		)
#		endif
		
		u8 wasm [44] = {
		  0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x06, 0x01, 0x60, 0x00, 0x02, 0x7f, 0x7d, 0x03, 0x02, 0x01, 0x00, 0x07, 0x08, 0x01, 0x04,
		  0x6d, 0x61, 0x69, 0x6e, 0x00, 0x00, 0x0a, 0x0c, 0x01, 0x0a, 0x00, 0x41, 0xd2, 0x09, 0x43, 0x33, 0x77, 0xb1, 0x45, 0x0b
		};
		  
		IM3Module module;
		result = m3_ParseModule  (env, & module, wasm, 44, false);		   			    expect (result == m3Err_none)
	
		result = m3_LoadModule (runtime, module);                                       expect (result == m3Err_none)

		IM3Function function = NULL;
		result = m3_FindFunction (& function, runtime, "main");							expect (result == m3Err_none)
																						expect (function)
		printf ("\n%s\n", result);

		if (function)
		{
			result = m3_CallV (function);                                               expect (result == m3Err_none)

			i32 ret0 = 0;
			f32 ret1 = 0.;
			m3_GetResultsV (function, & ret0, & ret1);
			
			printf ("%d %f\n", ret0, ret1);
		}
	}

		
	Test (multireturn.branch)
	{
#			if 0
			(module
			  (func (param i32) (result i32 i32)

				i32.const 123
				i32.const 456
				i32.const 789

				block (param i32 i32) (result i32 i32 i32)

					local.get 0
					local.get 0

					local.get 0
					br_if 0

					drop

				end

				drop
				drop
			  )

			(export "main" (func 0))
			)
#			endif
	}
    
    return 0;
}
