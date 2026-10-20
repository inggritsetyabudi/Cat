#include <stddef.h>
#include <stdio.h>

#if defined(_WIN32)
#include <_mingw.h>
#if !defined(__MINGW64__) || !defined(_UCRT) || !defined(_WIN64)
#error "the Windows oracle requires native x64 MinGW-w64 UCRT"
#endif
#elif defined(__linux__)
#if !defined(__x86_64__)
#error "the Linux oracle requires x86-64"
#endif
#else
#error "the oracle supports only native Linux x64 or Windows UCRT x64"
#endif

#include "../../../runtime/native_runtime.c"

_Static_assert(sizeof(void *) == 8, "64-bit pointer required");
_Static_assert(sizeof(claw_slice) == 16, "Str size");
_Static_assert(_Alignof(claw_slice) == 8, "Str alignment");
_Static_assert(offsetof(claw_slice, ptr) == 0, "Str ptr offset");
_Static_assert(offsetof(claw_slice, len) == 8, "Str len offset");
_Static_assert(sizeof(claw_buffer) == 24, "String size");
_Static_assert(_Alignof(claw_buffer) == 8, "String alignment");
_Static_assert(offsetof(claw_buffer, ptr) == 0, "String ptr offset");
_Static_assert(offsetof(claw_buffer, len) == 8, "String len offset");
_Static_assert(offsetof(claw_buffer, cap) == 16, "String cap offset");

int main(void) {
    printf("Str size=%zu align=%zu ptr=%zu len=%zu\n",
           sizeof(claw_slice), _Alignof(claw_slice),
           offsetof(claw_slice, ptr), offsetof(claw_slice, len));
    printf("String size=%zu align=%zu ptr=%zu len=%zu cap=%zu\n",
           sizeof(claw_buffer), _Alignof(claw_buffer),
           offsetof(claw_buffer, ptr), offsetof(claw_buffer, len),
           offsetof(claw_buffer, cap));
    return 0;
}
