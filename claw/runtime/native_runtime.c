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

// Arena Runtime Support
typedef struct claw_arena {
    unsigned char* start;
    unsigned char* current;
    unsigned char* end;
    struct claw_arena* next;
} claw_arena;

#define CLAW_ARENA_BLOCK_SIZE 65536

static claw_arena* claw_arena_allocate_block(size_t min_size) {
    size_t request = min_size > CLAW_ARENA_BLOCK_SIZE ? min_size : CLAW_ARENA_BLOCK_SIZE;
    // We allocate the header plus the space in one go
    claw_arena* block = (claw_arena*)malloc(sizeof(claw_arena) + request);
    if (!block) {
        fputs("fatal: Arena memory allocation failed\n", stderr);
        abort();
    }
    block->start = (unsigned char*)(block + 1);
    block->current = block->start;
    block->end = block->start + request;
    block->next = NULL;
    return block;
}

void* claw_runtime_arena_new(void) __asm__(CLAW_RT_ASM(claw.runtime.arena.new));
void claw_runtime_arena_free(void* ptr) __asm__(CLAW_RT_ASM(claw.runtime.arena.free));
void* claw_runtime_arena_alloc(void* arena_ptr, int64_t size, int64_t alignment) __asm__(CLAW_RT_ASM(claw.runtime.arena.alloc));
void claw_runtime_arena_reset(void* arena_ptr) __asm__(CLAW_RT_ASM(claw.runtime.arena.reset));

void* claw_runtime_arena_new(void) {
    return claw_arena_allocate_block(CLAW_ARENA_BLOCK_SIZE);
}

void claw_runtime_arena_free(void* ptr) {
    claw_arena* current = (claw_arena*)ptr;
    while (current != NULL) {
        claw_arena* next = current->next;
        free(current);
        current = next;
    }
}

void* claw_runtime_arena_alloc(void* arena_ptr, int64_t size, int64_t alignment) {
    claw_arena* head = (claw_arena*)arena_ptr;
    if (size <= 0) size = 1;
    if (alignment <= 0) alignment = 1;
    
    // Find the first block that can satisfy the allocation
    claw_arena* current = head;
    while (current != NULL) {
        uintptr_t current_addr = (uintptr_t)current->current;
        uintptr_t offset = current_addr % (uintptr_t)alignment;
        uintptr_t padding = offset == 0 ? 0 : (uintptr_t)alignment - offset;
        
        if ((size_t)(current->end - current->current) >= padding + (size_t)size) {
            void* result = current->current + padding;
            current->current += padding + (size_t)size;
            return result;
        }
        
        if (current->next == NULL) {
            break; // No more blocks, 'current' is the tail
        }
        current = current->next;
    }
    
    // Need a new block
    claw_arena* new_block = claw_arena_allocate_block(size + alignment);
    current->next = new_block;
    
    uintptr_t current_addr = (uintptr_t)new_block->current;
    uintptr_t offset = current_addr % (uintptr_t)alignment;
    uintptr_t padding = offset == 0 ? 0 : (uintptr_t)alignment - offset;
    
    void* result = new_block->current + padding;
    new_block->current += padding + (size_t)size;
    return result;
}

void claw_runtime_arena_reset(void* arena_ptr) {
    claw_arena* current = (claw_arena*)arena_ptr;
    while (current != NULL) {
        current->current = current->start;
        current = current->next;
    }
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
_Bool claw_runtime_str_starts_with(const claw_slice* text, const claw_slice* prefix) __asm__(CLAW_RT_ASM(claw.runtime.str.starts_with));
_Bool claw_runtime_str_ends_with(const claw_slice* text, const claw_slice* suffix) __asm__(CLAW_RT_ASM(claw.runtime.str.ends_with));
_Bool claw_runtime_str_contains(const claw_slice* text, const claw_slice* needle) __asm__(CLAW_RT_ASM(claw.runtime.str.contains));

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

static _Bool claw_runtime_slice_is_valid(const claw_slice* value) {
    return value != NULL && value->len >= 0 && (value->len == 0 || value->ptr != NULL);
}

_Bool claw_runtime_str_starts_with(const claw_slice* text, const claw_slice* prefix) {
    if (!claw_runtime_slice_is_valid(text) || !claw_runtime_slice_is_valid(prefix) || prefix->len > text->len) {
        return false;
    }
    return prefix->len == 0 || memcmp(text->ptr, prefix->ptr, (size_t)prefix->len) == 0;
}

_Bool claw_runtime_str_ends_with(const claw_slice* text, const claw_slice* suffix) {
    if (!claw_runtime_slice_is_valid(text) || !claw_runtime_slice_is_valid(suffix) || suffix->len > text->len) {
        return false;
    }
    if (suffix->len == 0) {
        return true;
    }
    const int64_t offset = text->len - suffix->len;
    return memcmp(text->ptr + offset, suffix->ptr, (size_t)suffix->len) == 0;
}

_Bool claw_runtime_str_contains(const claw_slice* text, const claw_slice* needle) {
    if (!claw_runtime_slice_is_valid(text) || !claw_runtime_slice_is_valid(needle)) {
        return false;
    }
    if (needle->len == 0) {
        return true;
    }
    if (needle->len > text->len) {
        return false;
    }
    const int64_t last_start = text->len - needle->len;
    for (int64_t start = 0; start <= last_start; ++start) {
        if (memcmp(text->ptr + start, needle->ptr, (size_t)needle->len) == 0) {
            return true;
        }
    }
    return false;
}
