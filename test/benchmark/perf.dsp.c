//
//  perf.dsp.c
//  m3
//
//  Created by Steven Massey on 4/30/19.
//  Copyright © 2019 Steven Massey. All rights reserved.
//

#include <inttypes.h>
# ifdef __EMSCRIPTEN__
# 	include "m3_host.h"
# else
#	include <stdio.h>
# endif

//class Noise
//{
//	public:
//
//	double Render ()
//	{
//		a ^= b;
//		b += a;
//
//		return scaler * b;
//	}
//
//	const double scaler = 1. / 0x7fffffffffffffff;
//
//	int64_t a = 0x67452301, b = 0xefcdab89;
//};


typedef struct Noise
{
	int64_t a, b;
}
Noise;

double Noise_Render (Noise * i_noise)
{
	const double scaler = 1. / 0x7fffffffffffffff;

	i_noise->a ^= i_noise->b;
	i_noise->b += i_noise->a;

	return scaler * i_noise->a;
}

int main ()
{
	Noise noise = { 0x67452301, 0xefcdab89 };
	
	const int s = 1000000000;
	
//	double * a = new double [s];
	
	double a;
	for (int i = 0; i < s; ++i)
	{
		a = Noise_Render (& noise);
//		m3Export (& a, 0);
//		a  = noise.Render ();
	}

	#ifdef __EMSCRIPTEN__
		m3Export (& a, 0);
	#else
		printf ("\n%lf\n", a);
	#endif
	
}

