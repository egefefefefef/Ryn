#include "ryn_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Formatting helpers                                                 */
/* ------------------------------------------------------------------ */

static void write_stdout(const char *text) {
    fputs(text, stdout);
}

static void write_signed(long long value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%lld", value);
    write_stdout(buffer);
}

static void write_unsigned(unsigned long long value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%llu", value);
    write_stdout(buffer);
}

/* Ryn prints floating point values with a trailing ".0" when they are whole
 * numbers, so that 1.0 does not look like an integer. */
static void write_double(double value, int precision) {
    char buffer[64];
    if (precision > 0 && precision < 17) {
        snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
    } else {
        snprintf(buffer, sizeof(buffer), "%.17g", value);
    }
    bool has_marker = false;
    for (const char *p = buffer; *p; p++) {
        if (*p == '.' || *p == 'e' || *p == 'E' || *p == 'n' || *p == 'N' || *p == 'i' ||
            *p == 'I') {
            has_marker = true;
            break;
        }
    }
    write_stdout(buffer);
    if (!has_marker) {
        write_stdout(".0");
    }
}

static void write_bool(bool value) {
    write_stdout(value ? "true" : "false");
}

static void write_char(uint8_t value) {
    fputc((int)value, stdout);
}

static void write_pointer(const void *value) {
    if (!value) {
        write_stdout("null");
        return;
    }
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%p", value);
    write_stdout(buffer);
}

static void newline(void) {
    fputc('\n', stdout);
}

/* ------------------------------------------------------------------ */
/* core                                                               */
/* ------------------------------------------------------------------ */

RYN_NORETURN void ryn_panic(const char *message) {
    fflush(stdout);
    fprintf(stderr, "panic: %s\n", message ? message : "(no message)");
    fflush(stderr);
    exit(1);
}

RYN_NORETURN void ryn_panic_div_zero(void) {
    ryn_panic("division by zero");
}

void ryn_assert(bool condition) {
    if (!condition) {
        ryn_panic("assertion failed");
    }
}

void ryn_assert_msg(bool condition, const char *message) {
    if (!condition) {
        ryn_panic(message);
    }
}

/* ------------------------------------------------------------------ */
/* print family                                                       */
/* ------------------------------------------------------------------ */

#define RYN_DEFINE_SIGNED_PRINT(suffix, type)                    \
    void ryn_print_##suffix(type value) {                        \
        write_signed((long long)value);                          \
    }                                                            \
    void ryn_println_##suffix(type value) {                      \
        write_signed((long long)value);                          \
        newline();                                               \
    }

#define RYN_DEFINE_UNSIGNED_PRINT(suffix, type)                  \
    void ryn_print_##suffix(type value) {                        \
        write_unsigned((unsigned long long)value);               \
    }                                                            \
    void ryn_println_##suffix(type value) {                      \
        write_unsigned((unsigned long long)value);               \
        newline();                                               \
    }

RYN_DEFINE_SIGNED_PRINT(i8, int8_t)
RYN_DEFINE_SIGNED_PRINT(i16, int16_t)
RYN_DEFINE_SIGNED_PRINT(i32, int32_t)
RYN_DEFINE_SIGNED_PRINT(i64, int64_t)
RYN_DEFINE_UNSIGNED_PRINT(u8, uint8_t)
RYN_DEFINE_UNSIGNED_PRINT(u16, uint16_t)
RYN_DEFINE_UNSIGNED_PRINT(u32, uint32_t)
RYN_DEFINE_UNSIGNED_PRINT(u64, uint64_t)

void ryn_print_f32(float value) {
    write_double((double)value, 7);
}

void ryn_println_f32(float value) {
    write_double((double)value, 7);
    newline();
}

void ryn_print_f64(double value) {
    write_double(value, 15);
}

void ryn_println_f64(double value) {
    write_double(value, 15);
    newline();
}

void ryn_print_bool(bool value) {
    write_bool(value);
}

void ryn_println_bool(bool value) {
    write_bool(value);
    newline();
}

void ryn_print_char(uint8_t value) {
    write_char(value);
}

void ryn_println_char(uint8_t value) {
    write_char(value);
    newline();
}

void ryn_print_str(const char *value) {
    write_stdout(value ? value : "(null)");
}

void ryn_println_str(const char *value) {
    write_stdout(value ? value : "(null)");
    newline();
}

void ryn_print_ptr(const void *value) {
    write_pointer(value);
}

void ryn_println_ptr(const void *value) {
    write_pointer(value);
    newline();
}

#undef RYN_DEFINE_SIGNED_PRINT
#undef RYN_DEFINE_UNSIGNED_PRINT

/* Silence unused helper warnings when only some helpers are used. */
void ryn_runtime_keep_helpers(void) {
    (void)write_double;
    (void)write_char;
    (void)write_pointer;
    (void)write_bool;
    (void)write_signed;
    (void)write_unsigned;
}
