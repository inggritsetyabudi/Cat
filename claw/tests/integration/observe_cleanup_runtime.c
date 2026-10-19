/* Test-only relink oracle for the two minimal String cleanup fixtures.
 * Load libc headers before replacing free so their declarations are untouched.
 * Every observed cleanup still calls the real free; nothing is suppressed.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void observe_cleanup_free(void* pointer) {
    if (pointer != NULL) {
        puts("cleanup");
    }
    free(pointer);
}

#define free observe_cleanup_free
#include "../../runtime/native_runtime.c"
#undef free
