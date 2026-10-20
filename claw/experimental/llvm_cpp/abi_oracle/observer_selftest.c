#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *claw_runtime_string_alloc(int64_t capacity)
    __asm__("claw.runtime.string.alloc");
void claw_runtime_string_copy(void *destination, const unsigned char *source,
                              int64_t length)
    __asm__("claw.runtime.string.copy");
void claw_runtime_string_free(void *pointer)
    __asm__("claw.runtime.string.free");

int main(int argc, char **argv) {
    const char payload[] = "oracle";
    const char *mode = (argc > 1) ? argv[1] : "normal";
    void *storage = claw_runtime_string_alloc((int64_t)(sizeof(payload) - 1));
    claw_runtime_string_copy(storage, (const unsigned char *)payload,
                             (int64_t)(sizeof(payload) - 1));
    if (memcmp(storage, payload, sizeof(payload) - 1) != 0) {
        fputs("observer-selftest:copy-failed\n", stderr);
        claw_runtime_string_free(storage);
        return 2;
    }

    if (strcmp(mode, "leak") == 0) {
        puts("observer-selftest:leak");
        return 0;
    }
    if (strcmp(mode, "untracked") == 0) {
        void *external = malloc(17);
        if (external == NULL) {
            return 3;
        }
        claw_runtime_string_free(external);
        claw_runtime_string_free(storage);
        puts("observer-selftest:untracked");
        return 0;
    }
    if (strcmp(mode, "normal") != 0) {
        fputs("observer-selftest: unknown mode\n", stderr);
        claw_runtime_string_free(storage);
        return 64;
    }

    claw_runtime_string_free(storage);
    puts("observer-selftest:ok");
    return 0;
}
