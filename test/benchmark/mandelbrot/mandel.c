#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

#include "colors.h"

double get_time() {
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

int main(int argc, char **argv) {
    unsigned width, height;
    double magn;
    if (argc <= 1) {
        width = 1024;
        height = width;
        magn  = 4e5;
    } else if (argc == 3) {
        width = atoi(argv[1]);
        height = width;
        magn = strtod(argv[2], NULL);
    } else {
        fprintf(stderr, "usage: %s <size> <magnification>\n", argv[0]);
        return 1;
    }
    double eps = 1e-17;
    double Q1LOG2 = 1.44269504088896340735992468100189213742664595415299;
    double LOG2 = 0.69314718055994530941723212145817656807550013436026;

    int x, y;
    double centerx = -0.743643887037158704752191506114774;
    double centery =  0.131825904205311970493132056385139;
    double bailout = 128;
    double logLogBailout = log(log(bailout));
    int foundperiods = 0;
    long maxiter = width * sqrt(magn);
    double x0d = 4 / magn / width;
    double x2 = -2 / magn + centerx;
    double y1d = -4 / magn / width;
    double y2 = 2 / magn * height / width + centery;

    double tbeg = get_time();

    // write out image header
    printf("P6 %d %d 255\n", width, height);

    for (y = 0; y < height; y++) {
        fprintf(stderr, "\r%.2f%%", (float)(y+1)/(height)*100);
        for (x = 0; x < width; x++) {
            double px = x*x0d + x2;
            double py = y*y1d + y2;
            // no Main bulb or Cardoid check to be faster
            double zx = px;
            double zy = py;
            long i;
            // Initial maximum period to detect.
            int check = 3;
            // Maximum period doubles every iterations:
            int whenupdate = 10;
            // Period history registers.
            double hx = 0;
            double hy = 0;
            double xx, yy;
            bool inside = true;
            for (i = 1; i <= maxiter; i++) {
                xx = zx * zx;
                yy = zy * zy;
                if (xx + yy > bailout) {
                    inside = false;
                    break;
                }
                // iterate
                zy = 2 * zx * zy + py;
                zx = xx - yy + px;

                // periodicity check
                double d = zx - hx;
                if (d > 0.0 ? d < eps : d > -eps) {
                    d = zy - hy;
                    if (d > 0.0 ? d < eps : d > -eps) {
                        // Period found.
                        foundperiods++;
                        break;
                    }
                }
                if ((i & check) == 0) {
                    if (--whenupdate == 0) {
                        whenupdate = 10;
                        check <<= 1;
                        check++;
                    }
                    // period = 0;
                    hx = zx;
                    hy = zy;
                }
            }

            if (inside) {
                const char black[3] = {};
                fwrite(black, 1, 3, stdout);
            } else {
                double r = sqrtl(zx*zx + zy*zy);
                double c = i - 1.28 + (logLogBailout - logl(logl(r))) * Q1LOG2;
                int idx = fmodl((logl(c/64+1)/LOG2+0.45), 1)*(GRADIENTLENGTH-1) + 0.5;
                fwrite(&colors[idx], 1, 3, stdout);
            }
        }
    }

    double tend = get_time();

    fprintf(stderr, "\nElapsed time: %.2f ms\n", tend-tbeg);

    //fprintf(stderr, "\n%d periods found\n", foundperiods);

    return 0;
}
