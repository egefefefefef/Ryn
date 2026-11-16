/*
 * Ryn runtime — public interface used by generated C11 code.
 *
 * All functions here have a working implementation in this directory. The
 * compiler links these translation units into every produced executable.
 */
#ifndef RYN_RUNTIME_H
#define RYN_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  define RYN_NORETURN __declspec(noreturn)
#else
#  define RYN_NORETURN _Noreturn
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* core                                                               */
/* ------------------------------------------------------------------ */

RYN_NORETURN void ryn_panic(const char *message);
RYN_NORETURN void ryn_panic_div_zero(void);
void ryn_assert(bool condition);
void ryn_assert_msg(bool condition, const char *message);

#define RYN_DECL_PRINT(suffix, type)          \
    void ryn_print_##suffix(type value);      \
    void ryn_println_##suffix(type value);

RYN_DECL_PRINT(i8, int8_t)
RYN_DECL_PRINT(i16, int16_t)
RYN_DECL_PRINT(i32, int32_t)
RYN_DECL_PRINT(i64, int64_t)
RYN_DECL_PRINT(u8, uint8_t)
RYN_DECL_PRINT(u16, uint16_t)
RYN_DECL_PRINT(u32, uint32_t)
RYN_DECL_PRINT(u64, uint64_t)
RYN_DECL_PRINT(f32, float)
RYN_DECL_PRINT(f64, double)
RYN_DECL_PRINT(bool, bool)
RYN_DECL_PRINT(char, uint8_t)
RYN_DECL_PRINT(ptr, const void *)

#undef RYN_DECL_PRINT

/* Strings are printed directly, without a length argument. */
void ryn_print_str(const char *value);
void ryn_println_str(const char *value);

/* ------------------------------------------------------------------ */
/* io                                                                 */
/* ------------------------------------------------------------------ */

/* Returns a NUL terminated line without the trailing newline. Returns an
 * empty string at end of input. The returned buffer is owned by the runtime. */
const char *ryn_io_read_line(void);
int32_t ryn_io_read_int(void);
double ryn_io_read_float(void);
void ryn_io_flush(void);

/* ------------------------------------------------------------------ */
/* math                                                               */
/* ------------------------------------------------------------------ */

double ryn_math_sqrt(double x);
double ryn_math_cbrt(double x);
double ryn_math_sin(double x);
double ryn_math_cos(double x);
double ryn_math_tan(double x);
double ryn_math_asin(double x);
double ryn_math_acos(double x);
double ryn_math_atan(double x);
double ryn_math_exp(double x);
double ryn_math_ln(double x);
double ryn_math_log2(double x);
double ryn_math_log10(double x);
double ryn_math_floor(double x);
double ryn_math_ceil(double x);
double ryn_math_round(double x);
double ryn_math_trunc(double x);
double ryn_math_fabs(double x);
double ryn_math_pow(double base, double exponent);
double ryn_math_atan2(double y, double x);
double ryn_math_fmod(double x, double y);
double ryn_math_hypot(double x, double y);
double ryn_math_min_f64(double a, double b);
double ryn_math_max_f64(double a, double b);
int32_t ryn_math_min_i32(int32_t a, int32_t b);
int32_t ryn_math_max_i32(int32_t a, int32_t b);
int64_t ryn_math_min_i64(int64_t a, int64_t b);
int64_t ryn_math_max_i64(int64_t a, int64_t b);
int32_t ryn_math_abs_i32(int32_t x);
int64_t ryn_math_abs_i64(int64_t x);

/* ------------------------------------------------------------------ */
/* str                                                                */
/* ------------------------------------------------------------------ */

uint64_t ryn_str_len(const char *s);
void ryn_str_free(const char *s);
bool ryn_str_eq(const char *a, const char *b);
const char *ryn_str_concat(const char *a, const char *b);
const char *ryn_str_from_int(int64_t value);
uint8_t ryn_str_index(const char *s, uint64_t index);

/* ------------------------------------------------------------------ */
/* mem                                                                */
/* ------------------------------------------------------------------ */

void *ryn_mem_alloc(uint64_t size);
void ryn_mem_free(void *pointer);
void *ryn_mem_realloc(void *pointer, uint64_t size);
void ryn_mem_copy(void *destination, const void *source, uint64_t size);
void ryn_mem_zero(void *destination, uint64_t size);

#ifdef __cplusplus
}
#endif

#endif /* RYN_RUNTIME_H */
