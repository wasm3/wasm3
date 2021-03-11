#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

#include "colors.h"
#include "doubledouble.h"

double get_time() {
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

int main(int argc, char **argv) {
    unsigned width, height;
    DoubleDouble magn;
    if (argc <= 1) {
        width = 1024;
        height = width;
        magn = dd_new(4e5, 0);
    } else if (argc == 3) {
        width = atoi(argv[1]);
        height = width;
        magn = dd_new(strtod(argv[2], NULL), 0);
    } else {
        fprintf(stderr, "usage: %s <size> <magnification>\n", argv[0]);
        return 1;
    }
    DoubleDouble temp1;
    double eps = 1e-17;
    double Q1LOG2 = 1.44269504088896340735992468100189213742664595415299;
    double LOG2 = 0.69314718055994530941723212145817656807550013436026;
    unsigned int x, y;
    DoubleDouble centerx, centery;
    centerx = dd_new(-0.7436438870371587, -3.628952515063387E-17);
    centery = dd_new(0.13182590420531198, -1.2892807754956678E-17);
    double bailout = 128; // with a smaller value there are lines on magn=1
    double logLogBailout = log(log(bailout));
    int foundperiods = 0;
    long maxiter = 50000;
    /*// maxiter = width * sqrt(magn);
    temp1 = dd_sqrt(magn);
    unsigned long maxiter = width * dd_get_ui(temp1);*/
    DoubleDouble x2, y2, x0d, y1d;
    // x0d = 4 / magn / width;
    x0d = dd_ui_div(4, magn);
    x0d = dd_div_ui(x0d, width);
    // x2 = -2 / magn + centerx;
    x2 = dd_si_div(-2, magn);
    x2 = dd_add(x2, centerx);
    // y1d = -4 / magn / width;
    y1d = dd_si_div(-4, magn);
    y1d = dd_div_ui(y1d, width);
    // y2 = 2 / magn * height / width + centery;
    y2 = dd_ui_div(2, magn);
    temp1 = dd_new(height, 0);
    temp1 = dd_div_ui(temp1, width);
    y2 = dd_mul(y2, temp1);
    y2 = dd_add(y2, centery);
    DoubleDouble px, py, zx, zy, xx, yy;
    double hx, hy, d;

    double tbeg = get_time();

    // write out image header
    printf("P6 %d %d 255\n", width, height);

    for (y = 0; y < height; y++) {
        fprintf(stderr, "\r%.2f%%", (float)(y+1)/(height)*100);
        for (x = 0; x < width; x++) {
            //px = x*x0d + x2;
            px = dd_mul_ui(x0d, x);
            px = dd_add(px, x2);
            //py = y*y1d + y2;
            py = dd_mul_ui(y1d, y);
            py = dd_add(py, y2);
            // no Main bulb or Cardoid check to be faster
            zx = dd_new(px.hi, px.lo);
            zy = dd_new(py.hi, py.lo);
            unsigned long i;
            bool inside = true;
            int check = 3;
            int whenupdate = 10;
            hx = 0;
            hy = 0;
            for (i = 1; i <= maxiter; i++) {
                //xx = zx * zx;
                xx = dd_sqr(zx);
                //yy = zy * zy;
                yy = dd_sqr(zy);
                //if (xx + yy > bailout) {
                if (xx.hi + yy.hi > bailout) {
                    inside = false;
                    break;
                }
                // iterate
                //zy = 2 * zx * zy + py;
                //zx = dd_mul_ui(zx, 2);
                //zy = dd_mul(zx, zy);
                zy = dd_add(dd_mul2(zx, zy), py);
                //zx = xx - yy + px;
                zx = dd_add(dd_sub(xx, yy), px);

                // period checking
                d = zx.hi - hx;
                if (d > 0.0 ? d < eps : d > -eps) {
                    d = zy.hi - hy;
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
                    hx = zx.hi;
                    hy = zy.hi;
                }
            }

            if (inside) {
                const char black[3] = {};
                fwrite(black, 1, 3, stdout);
            } else {
                double r = sqrt(zx.hi*zx.hi + zy.hi*zy.hi);
                double c = i - 1.28 + (logLogBailout - log(log(r))) * Q1LOG2;
                int idx = fmod((log(c/64+1)/LOG2+0.45), 1)*(GRADIENTLENGTH-1) + 0.5;
                fwrite(&colors[idx], 1, 3, stdout);
            }
        }
    }

    double tend = get_time();

    fprintf(stderr, "\nElapsed time: %.2f ms\n", tend-tbeg);

    //fprintf(stderr, "\n%d periods found\n", foundperiods);

    return 0;
}
