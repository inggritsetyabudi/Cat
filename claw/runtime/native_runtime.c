#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct claw_slice {
    const unsigned char* ptr;
    int64_t len;
} claw_slice;

typedef struct claw_buffer {
    unsigned char* ptr;
    int64_t len;
    int64_t cap;
} claw_buffer;

static void claw_write_bytes(const unsigned char* ptr, int64_t len) {
    if (ptr == NULL || len <= 0) {
        return;
    }
    fwrite(ptr, 1, (size_t)len, stdout);
}

static void claw_write_i128(__int128 value) {
    if (value == 0) {
        fputc('0', stdout);
        return;
    }

    unsigned __int128 magnitude = 0;
    if (value < 0) {
        fputc('-', stdout);
        magnitude = (unsigned __int128)(-(value + 1)) + 1;
    } else {
        magnitude = (unsigned __int128)value;
    }

    char buffer[48];
    size_t count = 0;
    while (magnitude > 0) {
        buffer[count++] = (char)('0' + (magnitude % 10));
        magnitude /= 10;
    }
    while (count > 0) {
        fputc(buffer[--count], stdout);
    }
}

#if defined(__APPLE__)
#define CLAW_RT_ASM(sym) "_" #sym
#else
#define CLAW_RT_ASM(sym) #sym
#endif

void* claw_runtime_anchor_alloc(int64_t size) __asm__(CLAW_RT_ASM(claw.runtime.anchor.alloc));
void claw_runtime_anchor_free(void* ptr) __asm__(CLAW_RT_ASM(claw.runtime.anchor.free));

void* claw_runtime_anchor_alloc(int64_t size) {
    const size_t request = size <= 0 ? 1u : (size_t)size;
    void* ptr = malloc(request);
    if (ptr == NULL) {
        fputs("fatal: Anchor allocation failed\n", stderr);
        abort();
    }
    return ptr;
}

void claw_runtime_anchor_free(void* ptr) {
    free(ptr);
}
static void claw_finish_print(bool newline) {
    if (newline) {
        fputc('\n', stdout);
    }
    fflush(stdout);
}

#if defined(__APPLE__)
#define CLAW_RT_ASM(sym) "_" #sym
#else
#define CLAW_RT_ASM(sym) #sym
#endif

void claw_runtime_print_i1(_Bool value) __asm__(CLAW_RT_ASM(claw.runtime.print.i1));
void claw_runtime_print_i8(int8_t value) __asm__(CLAW_RT_ASM(claw.runtime.print.i8));
void claw_runtime_print_i16(int16_t value) __asm__(CLAW_RT_ASM(claw.runtime.print.i16));
void claw_runtime_print_i32(int32_t value) __asm__(CLAW_RT_ASM(claw.runtime.print.i32));
void claw_runtime_print_i64(int64_t value) __asm__(CLAW_RT_ASM(claw.runtime.print.i64));
void claw_runtime_print_i128(__int128 value) __asm__(CLAW_RT_ASM(claw.runtime.print.i128));
void claw_runtime_print_float(float value) __asm__(CLAW_RT_ASM(claw.runtime.print.float));
void claw_runtime_print_double(double value) __asm__(CLAW_RT_ASM(claw.runtime.print.double));
void claw_runtime_print_ptr(void* value) __asm__(CLAW_RT_ASM(claw.runtime.print.ptr));
void claw_runtime_print_slice(const claw_slice* value) __asm__(CLAW_RT_ASM(claw.runtime.print.slice));
void claw_runtime_print_buffer(const claw_buffer* value) __asm__(CLAW_RT_ASM(claw.runtime.print.buffer));

void claw_runtime_println_i1(_Bool value) __asm__(CLAW_RT_ASM(claw.runtime.println.i1));
void claw_runtime_println_i8(int8_t value) __asm__(CLAW_RT_ASM(claw.runtime.println.i8));
void claw_runtime_println_i16(int16_t value) __asm__(CLAW_RT_ASM(claw.runtime.println.i16));
void claw_runtime_println_i32(int32_t value) __asm__(CLAW_RT_ASM(claw.runtime.println.i32));
void claw_runtime_println_i64(int64_t value) __asm__(CLAW_RT_ASM(claw.runtime.println.i64));
void claw_runtime_println_i128(__int128 value) __asm__(CLAW_RT_ASM(claw.runtime.println.i128));
void claw_runtime_println_float(float value) __asm__(CLAW_RT_ASM(claw.runtime.println.float));
void claw_runtime_println_double(double value) __asm__(CLAW_RT_ASM(claw.runtime.println.double));
void claw_runtime_println_ptr(void* value) __asm__(CLAW_RT_ASM(claw.runtime.println.ptr));
void claw_runtime_println_slice(const claw_slice* value) __asm__(CLAW_RT_ASM(claw.runtime.println.slice));
void claw_runtime_println_buffer(const claw_buffer* value) __asm__(CLAW_RT_ASM(claw.runtime.println.buffer));
_Bool claw_runtime_str_eq(const claw_slice* a, const claw_slice* b) __asm__(CLAW_RT_ASM(claw.runtime.str.eq));

void claw_runtime_print_i1(_Bool value) {
    fputs(value ? "true" : "false", stdout);
    claw_finish_print(false);
}

void claw_runtime_print_i8(int8_t value) {
    fprintf(stdout, "%d", (int)value);
    claw_finish_print(false);
}

void claw_runtime_print_i16(int16_t value) {
    fprintf(stdout, "%d", (int)value);
    claw_finish_print(false);
}

void claw_runtime_print_i32(int32_t value) {
    fprintf(stdout, "%d", value);
    claw_finish_print(false);
}

void claw_runtime_print_i64(int64_t value) {
    fprintf(stdout, "%lld", (long long)value);
    claw_finish_print(false);
}

void claw_runtime_print_i128(__int128 value) {
    claw_write_i128(value);
    claw_finish_print(false);
}

void claw_runtime_print_float(float value) {
    fprintf(stdout, "%.9g", (double)value);
    claw_finish_print(false);
}

void claw_runtime_print_double(double value) {
    fprintf(stdout, "%.17g", value);
    claw_finish_print(false);
}

void claw_runtime_print_ptr(void* value) {
    fprintf(stdout, "%p", value);
    claw_finish_print(false);
}

void claw_runtime_print_slice(const claw_slice* value) {
    if (value) claw_write_bytes(value->ptr, value->len);
    claw_finish_print(false);
}

void claw_runtime_print_buffer(const claw_buffer* value) {
    if (value) claw_write_bytes(value->ptr, value->len);
    claw_finish_print(false);
}

void claw_runtime_println_i1(_Bool value) {
    fputs(value ? "true" : "false", stdout);
    claw_finish_print(true);
}

void claw_runtime_println_i8(int8_t value) {
    fprintf(stdout, "%d\n", (int)value);
    fflush(stdout);
}

void claw_runtime_println_i16(int16_t value) {
    fprintf(stdout, "%d\n", (int)value);
    fflush(stdout);
}

void claw_runtime_println_i32(int32_t value) {
    fprintf(stdout, "%d\n", value);
    fflush(stdout);
}

void claw_runtime_println_i64(int64_t value) {
    fprintf(stdout, "%lld\n", (long long)value);
    fflush(stdout);
}

void claw_runtime_println_i128(__int128 value) {
    claw_write_i128(value);
    claw_finish_print(true);
}

void claw_runtime_println_float(float value) {
    fprintf(stdout, "%.9g\n", (double)value);
    fflush(stdout);
}

void claw_runtime_println_double(double value) {
    fprintf(stdout, "%.17g\n", value);
    fflush(stdout);
}

void claw_runtime_println_ptr(void* value) {
    fprintf(stdout, "%p\n", value);
    fflush(stdout);
}

void claw_runtime_println_slice(const claw_slice* value) {
    if (value) claw_write_bytes(value->ptr, value->len);
    claw_finish_print(true);
}

void claw_runtime_println_buffer(const claw_buffer* value) {
    if (value) claw_write_bytes(value->ptr, value->len);
    claw_finish_print(true);
}

_Bool claw_runtime_str_eq(const claw_slice* a, const claw_slice* b) {
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    if (a->len != b->len) return false;
    if (a->len == 0) return true;
    if (a->ptr == b->ptr) return true;
    return memcmp(a->ptr, b->ptr, (size_t)a->len) == 0;
}

