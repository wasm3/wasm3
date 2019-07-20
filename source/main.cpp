//
//
//  Created by Steven Massey on 4/15/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//


#include <stdio.h>
#include <stdlib.h>


#include "m3.hpp"

#include <time.h>

extern "C"
{
#	include "m3_core.h"
}

#include "m3_host.h"

void  m3Output  (const char * i_string)
{
	printf ("%s\n", i_string);
}


int  main  (int argc, const char * argv [])
{
	m3_PrintM3Info ();

	if (argc == 2)
	{
		FILE * f = fopen (argv [1], "rb");
		
		if (f)
		{
			fseek (f, 0, SEEK_END);
			size_t fsize = ftell (f);
			fseek (f, 0, SEEK_SET);
			
			if (fsize < 100000)
			{
				u8 * wasm = (u8 *) malloc (fsize);
				
				if (wasm)
				{
					fread (wasm, 1, fsize, f);
					fclose (f);
					
					IM3Module module;
					M3Result result = m3_ParseModule (& module, wasm, (u32) fsize);
					
					if (not result)
					{
						IM3Runtime env = m3_NewRuntime (32768);
						
						result = m3_LoadModule (env, module);
						
						if (not result)
						{
							m3_LinkFunction (module, "_printf", "v(**)", (void *) m3_printf);
							m3_LinkFunction (module, "_m3TestOut", "v(iFi)", (void *) m3TestOut);
							m3_LinkFunction (module, "_m3StdOut", "v(*)", (void *) m3Output);
							m3_LinkFunction (module, "_m3Export", "v(*i)", (void *) m3Export);
							m3_LinkFunction (module, "_m3Out_f64", "v(F)", (void *) m3Out_f64);
							m3_LinkFunction (module, "_m3Out_i32", "v(i)", (void *) m3Out_i32);
							m3_LinkFunction (module, "_TestReturn", "F(i)", (void *) TestReturn);

							m3_LinkFunction (module, "abortStackOverflow",	"v(i)",		(void *) m3_abort);
							m3_LinkFunction (module, "_malloc",				"i(Mi)",	(void *) m3_malloc);
							m3_LinkFunction (module, "_free",				"v(Mi)",	(void *) m3_free);
							m3_LinkFunction (module, "_memset",				"*(*ii)",	(void *) m3_memset);
							m3_LinkFunction (module, "_memcpy",				"*(**i)",	(void *) m3_memcpy);
							m3_LinkFunction (module, "_fopen",				"i(M**)",	(void *) m3_fopen);
							m3_LinkFunction (module, "_fread",				"i(*ii*)",	(void *) m3_fread);

							m3_PrintRuntimeInfo (env);

							IM3Function f;
							result = m3_FindFunction (& f, env, "__post_instantiate");
							if (not result)
								result = m3_Call (f);

							IM3Function main;
							result = m3_FindFunction (& main, env, "_main");
							
							if (not result and main)
							{
								printf ("found _main\n");

								clock_t start = clock ();

								result = m3_Call (main);
								
								clock_t end = clock ();
								double elapsed_time = (end - start) / (double) CLOCKS_PER_SEC ;
								printf("%lf\n", elapsed_time);
								
								printf ("call: %s\n", result);
								
								m3_PrintProfilerInfo ();
							}
							else printf ("find: %s\n", result);
						}
						else printf ("import: %s\n", result);
						
						if (result)
						{
							M3ErrorInfo info = m3_GetErrorInfo (env);
							printf ("%s\n", info.message);
						}
						
						m3_FreeRuntime (env);
					}
					else printf ("parse: %s\n", result);
				}
				
				free (wasm);
			}
		}
		else printf ("couldn't open '%s'\n", argv  [1]);
	}
	
	
	printf ("\n");
	
	return 0;
}
