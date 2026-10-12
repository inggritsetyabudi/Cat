#include <stdint.h>
#include <stdio.h>

typedef struct {
    int32_t *ptr;
    int64_t len;
} claw_slice;

extern int32_t update(claw_slice values) __asm__("revise_operator_index_compound::update");

int main(int argc, char **argv) {
    (void)argv;
    int32_t storage[1] = {41};
    claw_slice values = {storage, argc == 1 ? 1 : 0};
    const int32_t result = update(values);
    printf("%d\n%d\n", result, storage[0]);
    return 0;
}
