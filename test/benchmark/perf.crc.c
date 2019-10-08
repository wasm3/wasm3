
// emscripten compiler:
// emcc -s ERROR_ON_UNDEFINED_SYMBOLS=0 -O3 --profiling-funcs perf.crc.c crc.c -DCRC32 -I ../source/ -o crc.wasm

// gcc -O3 perf.crc.c crc.c -DCRC32 -I ../source/ -o crc



# ifdef __EMSCRIPTEN__
# include "m3_host.h"
# else
#	include <stdio.h>
# endif

# include <string.h>

# include "crc.h"


// CRC32= f4b868ef / 4105726191

const char * string = "We hold these truths to be self-evident, that all men are created equal, that they are endowed by their Creator with certain unalienable Rights, that among these are Life, Liberty and the pursuit of Happiness.--That to secure these rights, Governments are instituted among Men, deriving their just powers from the consent of the governed, --That whenever any Form of Government becomes destructive of these ends, it is the Right of the People to alter or to abolish it, and to institute new Government, laying its foundation on such principles and organizing its powers in such form, as to them shall seem most likely to effect their Safety and Happiness. Prudence, indeed, will dictate that Governments long established should not be changed for light and transient causes; and accordingly all experience hath shewn, that mankind are more disposed to suffer, while evils are sufferable, than to right themselves by abolishing the forms to which they are accustomed. But when a long train of abuses and usurpations, pursuing invariably the same Object evinces a design to reduce them under absolute Despotism, it is their right, it is their duty, to throw off such Government, and to provide new Guards for their future security.--Such has been the patient sufferance of these Colonies; and such is now the necessity which constrains them to alter their former Systems of Government. The history of the present King of Great Britain is a history of repeated injuries and usurpations, all having in direct object the establishment of an absolute Tyranny over these States. To prove this, let Facts be submitted to a candid world.";

const char * ss = "123";

const char * z = "0";


size_t StrLen (const char * i_string)
{
	const char * p = i_string;
	while (* p != 0)
		++p;

	return p - i_string;
}

int main ()
{
//	crcInit ();	

	uint32_t sum = 0;

	for (int i = 0; i < 50000; ++i)
	{
		sum += crcSlow ((unsigned char *) string, StrLen (string));
	}
	
//	sum = reflect (87, 8);

//	sum = StrLen (string);
	
//	;
	
//	sum = reflect (0x12345678, 32);

//	sum = strlen (string);

	# ifndef __EMSCRIPTEN__
		printf ("sum= %u %x\n", sum, sum);
	#else
		m3Out_i32 (sum);
	# endif

	return sum;
}
