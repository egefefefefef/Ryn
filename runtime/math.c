#include "ryn_runtime.h"

#include <math.h>

#define RYN_MATH1(name, function)     \
    double ryn_math_##name(double x) { \
        return function(x);           \
    }

RYN_MATH1(sqrt, sqrt)
RYN_MATH1(cbrt, cbrt)
RYN_MATH1(sin, sin)
RYN_MATH1(cos, cos)
RYN_MATH1(tan, tan)
RYN_MATH1(asin, asin)
RYN_MATH1(acos, acos)
RYN_MATH1(atan, atan)
RYN_MATH1(exp, exp)
RYN_MATH1(ln, log)
RYN_MATH1(log2, log2)
RYN_MATH1(log10, log10)
RYN_MATH1(floor, floor)
RYN_MATH1(ceil, ceil)
RYN_MATH1(round, round)
RYN_MATH1(trunc, trunc)
RYN_MATH1(fabs, fabs)

#undef RYN_MATH1

double ryn_math_pow(double base, double exponent) {
    return pow(base, exponent);
}

double ryn_math_atan2(double y, double x) {
    return atan2(y, x);
}

double ryn_math_fmod(double x, double y) {
    return fmod(x, y);
}

double ryn_math_hypot(double x, double y) {
    return hypot(x, y);
}

double ryn_math_min_f64(double a, double b) {
    return a < b ? a : b;
}

double ryn_math_max_f64(double a, double b) {
    return a > b ? a : b;
}

int32_t ryn_math_min_i32(int32_t a, int32_t b) {
    return a < b ? a : b;
}

int32_t ryn_math_max_i32(int32_t a, int32_t b) {
    return a > b ? a : b;
}

int64_t ryn_math_min_i64(int64_t a, int64_t b) {
    return a < b ? a : b;
}

int64_t ryn_math_max_i64(int64_t a, int64_t b) {
    return a > b ? a : b;
}

int32_t ryn_math_abs_i32(int32_t x) {
    return (int32_t)(x < 0 ? -(int64_t)x : (int64_t)x);
}

int64_t ryn_math_abs_i64(int64_t x) {
    if (x == INT64_MIN) {
        return INT64_MIN; /* |INT64_MIN| does not fit in an int64_t */
    }
    return x < 0 ? -x : x;
}
