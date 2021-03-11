#include <math.h>

typedef struct {
    double hi;
    double lo;
} DoubleDouble;

static inline DoubleDouble dd_new(double hi, double lo) {
    DoubleDouble num;
    num.hi = hi;
    num.lo = lo;
    return num;
}

unsigned int dd_get_ui(DoubleDouble num) {
    return num.hi;
}

static inline DoubleDouble dd_sqrt(DoubleDouble num) {
    double a, b, c, d, e;
    d = 1 / sqrt(num.hi);
    e = num.hi * d;
    a = 0x08000001 * e;
    a += e - a;
    b = e - a;
    c = e * e;
    b = ((a * a - c) + a * b * 2) + b * b;
    a = num.hi - c;
    c = num.hi - a;
    c = (a + ((((num.hi - (c + a)) + (c - c)) + num.lo) - b)) * d * 0.5;
    a = e + c;
    b = e - a;
    return dd_new(a, (e - (b + a)) + (b + c));
}

static inline DoubleDouble dd_div(DoubleDouble num1, DoubleDouble num2) {
    double a, b, c, d, e, f, g;
    f = num1.hi / num2.hi;
    a = 0x08000001 * num2.hi;
    a += num2.hi - a;
    b = num2.hi - a;
    c = 0x08000001 * f;
    c += f - c;
    d = f - c;
    e = num2.hi * f;
    c = (((a * c - e) + (a * d + b * c)) + b * d) + num2.lo * f;
    b = num1.lo - c;
    d = num1.lo - b;
    a = num1.hi - e;
    e = (num1.hi - ((num1.hi - a) + a)) + b;
    g = a + e;
    e += (a - g) + ((num1.lo - (d + b)) + (d - c));
    a = g + e;
    b = a / num2.hi;
    f += (e + (g - a)) / num2.hi;
    a = f + b;
    return dd_new(a, b + (f - a));
}

static inline DoubleDouble dd_ui_div(unsigned int num1, DoubleDouble num2) {
    return dd_div(dd_new(num1, 0), num2);
}

static inline DoubleDouble dd_div_ui(DoubleDouble num1, unsigned int num2) {
    return dd_div(num1, dd_new(num2, 0));
}

static inline DoubleDouble dd_si_div(signed int num1, DoubleDouble num2) {
    return dd_div(dd_new(num1, 0), num2);
}

static inline DoubleDouble dd_div_si(DoubleDouble num1, signed int num2) {
    return dd_div(num1, dd_new(num2, 0));
}

static inline DoubleDouble dd_add(DoubleDouble num1, DoubleDouble num2) {
    double a, b, c, d, e, f;
    e = num1.hi + num2.hi;
    d = num1.hi - e;
    a = num1.lo + num2.lo;
    f = num1.lo - a;
    d = ((num1.hi - (d + e)) + (d + num2.hi)) + a;
    b = e + d;
    c = ((num1.lo - (f + a)) + (f + num2.lo)) + (d + (e - b));
    a = b + c;
    return dd_new(a, c + (b - a));
}

static inline DoubleDouble dd_mul(DoubleDouble num1, DoubleDouble num2) {
    double a, b, c, d, e;
    a = 0x08000001 * num1.hi;
    a += num1.hi - a;
    b = num1.hi - a;
    c = 0x08000001 * num2.hi;
    c += num2.hi - c;
    d = num2.hi - c;
    e = num1.hi * num2.hi;
    c = (((a * c - e) + (a * d + b * c)) + b * d) + (num1.lo * num2.hi + num1.hi * num2.lo);
    a = e + c;
    return dd_new(a, c + (e - a));
}

static inline DoubleDouble dd_mul2(DoubleDouble num1, DoubleDouble num2) {
    double a, b, c, d, e;
    a = 0x08000001 * num1.hi;
    a += num1.hi - a;
    b = num1.hi - a;
    c = 0x08000001 * num2.hi;
    c += num2.hi - c;
    d = num2.hi - c;
    e = num1.hi * num2.hi;
    c = 2*((((a * c - e) + (a * d + b * c)) + b * d) + (num1.lo * num2.hi + num1.hi * num2.lo));
    a = 2*e + c;
    return dd_new(a, c + (2*e - a));
}

static inline DoubleDouble dd_mul_ui(DoubleDouble num1, unsigned int num2) {
    return dd_mul(num1, dd_new(num2, 0));
}

static inline DoubleDouble dd_mul_d(DoubleDouble num1, double num2) {
    return dd_mul(num1, dd_new(num2, 0));
}

static inline DoubleDouble dd_sub(DoubleDouble num1, DoubleDouble num2) {
    double a, b, c, d, e, f, g;
    g = num1.lo - num2.lo;
    f = num1.lo - g;
    e = num1.hi - num2.hi;
    d = num1.hi - e;
    d = ((num1.hi - (d + e)) + (d - num2.hi)) + g;
    b = e + d;
    c = (d + (e - b)) + ((num1.lo - (f + g)) + (f - num2.lo));
    a = b + c;
    return dd_new(a, c + (b - a));
}

static inline DoubleDouble dd_sqr(DoubleDouble num) {
    double a, b, c;
    a = 0x08000001 * num.hi;
    a += num.hi - a;
    b = num.hi - a;
    c = num.hi * num.hi;
    b = ((((a * a - c) + a * b * 2) + b * b) + num.hi * num.lo * 2) + num.lo * num.lo;
    a = b + c;
    return dd_new(a, b + (c - a));
}
