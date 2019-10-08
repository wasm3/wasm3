#ifndef __EMSCRIPTEN__
#	include <stdio.h>
#	include <string.h>
#	include <time.h>
#	define m3Out_f64(OUT)
#else
	#include "m3_host.h"
#endif

const int WIDTH = 1200;
const int HEIGHT = 800;
unsigned char image[WIDTH * HEIGHT * 3];

unsigned char colour(int iteration, int offset, int scale) {
  iteration = ((iteration * scale) + offset) % 1024;
  if (iteration < 256)
  {
//    return iteration;
  }
  else if (iteration < 512) {
    iteration = 255 - (iteration - 255);
  } else {
    iteration = 0;
  }
	
#ifndef __EMSCRIPTEN__
//	printf ("colour: %d %d\n", iteration, (int) (unsigned char) iteration);
#endif

	return iteration;
}

int iterateEquation(double x0, double y0, int maxiterations) {
	
#ifndef __EMSCRIPTEN__
//	printf ("iterateEquation: %lf %lf %d", x0, y0, maxiterations);
#endif

  double a = 0.0, b = 0.0, rx = 0.0, ry = 0.0;
  int iterations = 0;
  while (iterations < maxiterations && (rx * rx + ry * ry <= 4.0)) {
    rx = a * a - b * b + x0;
    ry = 2.0 * a * b + y0;
    a = rx;
    b = ry;
    iterations++;
  }
	
#ifndef __EMSCRIPTEN__
//	printf (" -> %d\n", iterations);
#endif

  return iterations;
}

// first pass: -0.743645 0.000293 120.000000 0.000000
double scale(double domainStart, double domainLength, double screenLength, double step)
{
	double s = domainStart + domainLength * ((step - screenLength) / screenLength);
	#ifndef __EMSCRIPTEN__
//		printf ("scale: %lf %lf %lf %lf -> %lf\n", domainStart, domainLength, screenLength, step, s);
	#endif
	
//	m3Out_f64 (s);
	
	return s;
}

void mandelbrot(int maxIterations, double cx, double cy, double diameter)
{
  double verticalDiameter = diameter * HEIGHT / WIDTH;
  for(double x = 0.0; x < WIDTH; x++) {
    for(double y = 0.0; y < HEIGHT; y++) {
		
		// map to mandelbrot coordinates
      double rx = scale (cx, diameter, WIDTH, x);
      double ry = scale (cy, verticalDiameter, HEIGHT, y);
		
      int iterations = iterateEquation(rx, ry, maxIterations);
      int idx = ((x + y * WIDTH) * 3);
		
//	m3Out_i32 (idx);
		
		// set the red and alpha components
      image[idx] = iterations == maxIterations ? 0 : colour(iterations, 0, 4);
      image[idx + 1] = iterations == maxIterations ? 0 : colour(iterations, 128, 4);
      image[idx + 2] = iterations == maxIterations ? 0 : colour(iterations, 356, 4);
    }
  }
}

unsigned char* getImage() {
  return &image[0];
}

#ifdef __EMSCRIPTEN__
#endif

int main() {

	int numLoops = 10;
	
#ifdef __EMSCRIPTEN__
  for (int i = 0; i < numLoops; i++)
  {
    mandelbrot(10000, -0.7436447860, 0.1318252536, 0.00029336);
  }

	m3Export (getImage (), WIDTH * HEIGHT * 3);
	
#else
  clock_t start = clock() ;

  for (int i = 0; i < numLoops; i++) {
    mandelbrot(10000, -0.7436447860, 0.1318252536, 0.00029336);
  }

  clock_t end = clock() ;
  double elapsed_time = (end-start)/(double)CLOCKS_PER_SEC ;
  printf("%lf\n", elapsed_time);

	FILE * f = fopen ("mandel.ppm", "wb");

	const char * header = "P6\n1200 800\n255\n";
	fwrite (header, 1, strlen (header), f);	

	fwrite (getImage (), 1, WIDTH * HEIGHT * 3, f);
	fclose (f);
#endif

  return 0;
}
