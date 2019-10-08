//
//  test.zlib.c
//  m3
//
//  Created by Steven Massey on 6/28/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include <stdlib.h>
#include <stdio.h>

#include "m3_host.h"
#include "zlib.h"

int main ()
{
	int srcLength = 804335;
	void * data = malloc (804335);
	
	uLongf length = 900000;
	void * dest = malloc (length);

	FILE * f = fopen ("/Users/smassey/Sync/Local/98-0.txt", "r+b");
	
	size_t s = 0;
	if (f)
	{
		size_t d = fread (data, 1, srcLength, f);

		m3Out_i32 (d);

//		ZEXTERN int ZEXPORT compress2 OF((Bytef *dest,   uLongf *destLen,
//										  const Bytef *source, uLong sourceLen,
//										  int level));

		s = compress2 (dest, & length, data, srcLength, 9);
	
	#ifdef __EMSCRIPTEN__
		m3Out_i32 (length);
		m3Out_i32 (s);
	#else
		printf ("%d -> %d result: %d\n", d, length, s);
	#endif
	}
	
	free (data);
	
	return s;
}
