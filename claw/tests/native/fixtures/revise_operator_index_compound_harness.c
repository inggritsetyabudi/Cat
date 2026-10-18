#include <stdint.h>
#include <stdio.h>

typedef struct {
    void *ptr;
    int64_t len;
} claw_slice;

#if defined(_WIN32)
extern int32_t forward_update(claw_slice *values) __asm__("revise_operator_index_compound::forward_update");
extern uint64_t forward_text_length(claw_slice *text) __asm__("revise_operator_index_compound::forward_text_length");
#else
extern int32_t forward_update(claw_slice values) __asm__("revise_operator_index_compound::forward_update");
extern uint64_t forward_text_length(claw_slice text) __asm__("revise_operator_index_compound::forward_text_length");
#endif

int main(int argc, char **argv) {
    (void)argv;
    int32_t storage[1] = {41};
    claw_slice values = {storage, argc == 1 ? 1 : 0};
    static const char text[] = "Cat";
    claw_slice text_view = {(void *)text, 3};
#if defined(_WIN32)
    const int32_t result = forward_update(&values);
    const uint64_t text_length = forward_text_length(&text_view);
#else
    const int32_t result = forward_update(values);
    const uint64_t text_length = forward_text_length(text_view);
#endif
    printf("%d\n%d\n%llu\n", result, storage[0], (unsigned long long)text_length);
    return 0;
}
