//
//  m3_host.c
//  m3
//
//  Created by Steven Massey on 4/28/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include "m3_host.h"
#include "m3_core.h"
#include "m3_env.h"

#include <stdio.h>
#include <assert.h>

void m3_printf (cstr_t i_format, const void * i_varArgs)
{
	const size_t c_bufferLength = 1000;
	char format [c_bufferLength];
	char output [c_bufferLength];

	size_t formatLength = strlen (i_format) + 1;
	char * buffer = formatLength <= c_bufferLength ? format : malloc (formatLength);
	
	size_t numArgs = 0;
	char * p = 0;
	
	if (buffer)
	{
		memcpy (buffer, i_format, formatLength);
		
		char * p = buffer + formatLength - 1;
		
//		while (p >= buffer)
//		{
//			if (*p == '%')
//			{
//				
//			}
//		}
		
		
	//	cstr_t f = i_format;
	//	while (* f)
	//	{
	//		if (* f == '%')
	//			++argCount;
	//		++f;
	//	}

	//	printf (i_format, i_varArgs [0], i_varArgs [0]);
		
//		printf ("printf!!!!\n");
		
		printf (format);
		
		if (buffer != format)
			free (buffer);
	}
}


void  m3_abort  (i32 i_dunno)
{
	// FIX: return trap
	abort ();
}


i32 AllocateHeap (M3Memory * io_memory, i32 i_size)
{
	i_size = (i_size + 7) & ~7;
	size_t ptrOffset = io_memory->heapOffset + (io_memory->heapAllocated += i_size);

	size_t size = (u8 *) io_memory->mallocated->end - io_memory->wasmPages;
	
	assert (ptrOffset < size);

	return (i32) ptrOffset;
}


i32  m3_malloc  (IM3Module i_module, i32 i_size)
{
	i32 heapOffset = AllocateHeap (& i_module->memory, i_size);
	printf ("malloc module: %s size: %d off: %d %p\n", i_module->name, i_size, heapOffset, i_module->memory.wasmPages + heapOffset);
	
	return heapOffset;
}


void  m3_free  (IM3Module i_module, i32 i_data)
{
	printf ("malloc free: %s\n", i_module->name);
}


void *  m3_memset  (void * i_ptr, i32 i_value, i32 i_size)
{
	memset (i_ptr, i_value, i_size);
	return i_ptr;
}


void *  m3_memcpy  (void * o_dst, void * i_src, i32 i_size)
{
	return memcpy (o_dst, i_src, i_size);
}


i32  m3_fopen  (IM3Module i_module, ccstr_t i_path, ccstr_t i_mode)
{
	i32 offset = 0;
	
	printf ("fopen: %s '%s'\n", i_path, i_mode);
	
	FILE * file = fopen (i_path, i_mode);

	if (file)
	{
		offset = AllocateHeap (& i_module->memory, sizeof (FILE *));
		
		void ** ptr = (void **) (i_module->memory.wasmPages + offset);
		* ptr = file;
	}
	
	return offset;
}


// TODO: system calls should be able to return traps. make return first arg.

i32  m3_fread  (void * io_ptr, i32 i_size, i32 i_count, FILE * i_file)
{
	FILE * file = * (void **) i_file;
	
	return (i32) fread (io_ptr, i_size, i_count, file);
}


M3Result EmbedHost (IM3Runtime i_runtime)
{
	M3Result result = c_m3Err_none;
	
	return result;
}



double TestReturn (int32_t i_value)
{
	return i_value / 10.;
}

void  m3StdOut  (const char * const i_string)
{
	printf ("m3_out: %s", i_string);
}

void m3Out_f64 (double i_value)
{
	printf ("%lf\n", i_value);
}

void m3Out_i32 (i32 i_value)
{
	printf ("m3_out: %d %u\n", i_value, (u32) i_value);
}

void  m3TestOut  (int32_t i_int0, double i_double, int32_t i_int1)
{
	printf ("0: %d, 1: %lf, 2: %d\n", i_int0, i_double, i_int1);
}


void m3Export (const void * i_data, i32 i_size)
{
	f64 v = * (f64 *) i_data;
	
//	printf ("%lf\n", v);
	
	printf ("exporting: %p %d bytes\n", i_data, i_size);
	FILE * f = fopen ("wasm.mandel.ppm", "wb");

	const char * header = "P6\n1200 800\n255\n";
	fwrite (header, 1, strlen (header), f);

	fwrite (i_data, 1, i_size, f);
	fclose (f);
}


