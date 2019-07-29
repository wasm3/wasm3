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


int  main  (int i_argc, const char * i_argv [])
{
	M3Result result = c_m3Err_none;
	
	m3_PrintM3Info ();

	if (i_argc >= 2)
	{
		FILE * f = fopen (i_argv [1], "rb");
		
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
					IM3Runtime env = nullptr;
					
					try
					{
						fread (wasm, 1, fsize, f);
						fclose (f);
						
						IM3Module module;
						result = m3_ParseModule (& module, wasm, (u32) fsize); if (result) throw result;
						
						env = m3_NewRuntime (32768);
						
						result = m3_LoadModule (env, module); if (result) throw result;
						
						m3_LinkFunction (module, "_m3TestOut", "v(iFi)", (void *) m3TestOut);
						m3_LinkFunction (module, "_m3StdOut", "v(*)", (void *) m3Output);
						m3_LinkFunction (module, "_m3Export", "v(*i)", (void *) m3Export);
						m3_LinkFunction (module, "_m3Out_f64", "v(F)", (void *) m3Out_f64);
						m3_LinkFunction (module, "_m3Out_i32", "v(i)", (void *) m3Out_i32);
						m3_LinkFunction (module, "_TestReturn", "F(i)", (void *) TestReturn);

						m3_LinkFunction (module, "abortStackOverflow",	"v(i)",		(void *) m3_abort);
						
						result = m3_LinkCStd (module); if (result) throw result;

						m3_PrintRuntimeInfo (env);

						IM3Function f;
						result = m3_FindFunction (& f, env, "__post_instantiate"); //if (result) throw result;
						
						if (not result)
						{
							result = m3_Call (f); if (result) throw result;
						}

						IM3Function main;
						result = m3_FindFunction (& main, env, "_main"); if (result) throw result;
						
						if (main)
						{
							printf ("found _main\n");

							clock_t start = clock ();

//							result = m3_Call (main);
							
							if (i_argc)
							{
								--i_argc;
								++i_argv;
							}
							
							result = m3_CallWithArgs (main, i_argc, i_argv);
							
							clock_t end = clock ();
							double elapsed_time = (end - start) / (double) CLOCKS_PER_SEC ;
							printf("%lf\n", elapsed_time);
							
//							printf ("call: %s\n", result);
							
							m3_PrintProfilerInfo ();
						}
						
					}
					
					catch (const M3Result & r) {}

					if (result)
					{
						printf ("result: %s", result);
					
						if (env)
						{
							M3ErrorInfo info = m3_GetErrorInfo (env);
							printf (" (%s)", info.message);
						}
						
						printf ("\n");
					}
					
					m3_FreeRuntime (env);
				}
				
				free (wasm);
			}
		}
		else printf ("couldn't open '%s'\n", i_argv [1]);
	}
	
	printf ("\n");
	
	return 0;
}
