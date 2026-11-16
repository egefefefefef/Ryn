#include "ryn_runtime.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Reads one line from stdin. The returned buffer belongs to the runtime and
 * stays valid until the process exits. */
static char *read_raw_line(void) {
    size_t capacity = 128;
    size_t length = 0;
    char *buffer = (char *)malloc(capacity);
    if (!buffer) {
        ryn_panic("out of memory while reading input");
    }
    int c;
    while ((c = fgetc(stdin)) != EOF) {
        if (c == '\n') {
            break;
        }
        if (c == '\r') {
            continue;
        }
        if (length + 1 >= capacity) {
            size_t next = capacity * 2;
            char *grown = (char *)realloc(buffer, next);
            if (!grown) {
                free(buffer);
                ryn_panic("out of memory while reading input");
            }
            buffer = grown;
            capacity = next;
        }
        buffer[length++] = (char)c;
    }
    buffer[length] = '\0';
    return buffer;
}

const char *ryn_io_read_line(void) {
    return read_raw_line();
}

static void fail_parse(const char *what, const char *line) {
    char message[256];
    snprintf(message, sizeof(message), "expected %s, got '%s'", what, line);
    ryn_panic(message);
}

int32_t ryn_io_read_int(void) {
    const char *line = read_raw_line();
    if (line[0] == '\0') {
        ryn_panic("expected an integer, but end of input was reached");
    }
    errno = 0;
    char *end = NULL;
    long long value = strtoll(line, &end, 10);
    if (end == line || *end != '\0' || errno == ERANGE) {
        fail_parse("an integer", line);
    }
    if (value < INT32_MIN || value > INT32_MAX) {
        ryn_panic("integer input does not fit in i32");
    }
    return (int32_t)value;
}

double ryn_io_read_float(void) {
    const char *line = read_raw_line();
    if (line[0] == '\0') {
        ryn_panic("expected a number, but end of input was reached");
    }
    errno = 0;
    char *end = NULL;
    double value = strtod(line, &end);
    if (end == line || *end != '\0' || errno == ERANGE) {
        fail_parse("a number", line);
    }
    return value;
}

void ryn_io_flush(void) {
    fflush(stdout);
}
