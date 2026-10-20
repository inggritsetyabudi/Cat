/* Test-only observer: the production runtime is included unchanged below. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define OBSERVER_SLOTS 8192u
static struct {
    void *pointer;
    size_t bytes;
} observer_slots[OBSERVER_SLOTS];
static size_t observer_allocations;
static size_t observer_frees;
static size_t observer_outstanding;
static size_t observer_untracked_frees;

static void observer_fail(const char *message) {
    fputs(message, stderr);
    fputc('\n', stderr);
    abort();
}

static void *observer_malloc(size_t bytes) {
    void *pointer = malloc(bytes);
    if (pointer == NULL) {
        return NULL;
    }
    size_t slot;
    for (slot = 0; slot < OBSERVER_SLOTS; ++slot) {
        if (observer_slots[slot].pointer == NULL) {
            break;
        }
    }
    if (slot == OBSERVER_SLOTS) {
        observer_fail("AUDIT registry exhausted");
    }
    if (observer_allocations == SIZE_MAX || observer_outstanding == SIZE_MAX) {
        observer_fail("AUDIT counter overflow");
    }
    observer_slots[slot].pointer = pointer;
    observer_slots[slot].bytes = bytes;
    ++observer_allocations;
    ++observer_outstanding;
    fprintf(stderr, "AUDIT alloc %zu\n", bytes);
    return pointer;
}

static void observer_free(void *pointer) {
    if (pointer != NULL) {
        size_t slot;
        for (slot = 0; slot < OBSERVER_SLOTS; ++slot) {
            if (observer_slots[slot].pointer == pointer) {
                break;
            }
        }
        if (slot == OBSERVER_SLOTS) {
            if (observer_untracked_frees == SIZE_MAX) {
                observer_fail("AUDIT counter overflow");
            }
            ++observer_untracked_frees;
        } else {
            if (observer_frees == SIZE_MAX || observer_outstanding == 0) {
                observer_fail("AUDIT counter overflow");
            }
            fprintf(stderr, "AUDIT free %zu\n", observer_slots[slot].bytes);
            observer_slots[slot].pointer = NULL;
            observer_slots[slot].bytes = 0;
            ++observer_frees;
            --observer_outstanding;
        }
    }
    free(pointer);
}

static void observer_report(void) {
    fprintf(stderr,
            "AUDIT summary allocations=%zu frees=%zu outstanding=%zu untracked_frees=%zu\n",
            observer_allocations, observer_frees, observer_outstanding,
            observer_untracked_frees);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
#endif
static void observer_init(void) {
    if (atexit(observer_report) != 0) {
        observer_fail("AUDIT atexit registration failed");
    }
}

/* Keep these macros active only while compiling the unmodified runtime TU. */
#define malloc observer_malloc
#define free observer_free
#include "../../../runtime/native_runtime.c"
#undef malloc
#undef free
