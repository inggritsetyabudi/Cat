#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN32 INT32_MIN

typedef struct {
    void *ptr;
    int64_t len;
} claw_slice;

extern int32_t implicit_tail(int32_t) __asm__("revise_integer_division_paths::implicit_tail");
extern int32_t argument_nesting(int32_t) __asm__("revise_integer_division_paths::argument_nesting");
extern int32_t if_selected_first(int32_t) __asm__("revise_integer_division_paths::if_selected_first");
extern int32_t pick_selected_first(int32_t) __asm__("revise_integer_division_paths::pick_selected_first");
extern int32_t if_selected_other(int32_t) __asm__("revise_integer_division_paths::if_selected_other");
extern int32_t pick_selected_other(int32_t) __asm__("revise_integer_division_paths::pick_selected_other");
extern int32_t and_or_first(int32_t) __asm__("revise_integer_division_paths::and_or_first");
extern int32_t loop_paths(int32_t) __asm__("revise_integer_division_paths::loop_paths");
extern int32_t loop_condition(int32_t) __asm__("revise_integer_division_paths::loop_condition");
extern int32_t scan_body(int32_t) __asm__("revise_integer_division_paths::scan_body");
extern int32_t tuple_projection(int32_t) __asm__("revise_integer_division_paths::tuple_projection");
extern int32_t shape_field(int32_t) __asm__("revise_integer_division_paths::shape_field");
extern int32_t array_int32(int32_t) __asm__("revise_integer_division_paths::array_int32");
extern int32_t array_uint8(int32_t) __asm__("revise_integer_division_paths::array_uint8");
extern int32_t min_binary(int32_t) __asm__("revise_integer_division_paths::min_binary");
extern int32_t min_remainder(int32_t) __asm__("revise_integer_division_paths::min_remainder");
extern int32_t short_and_skip(int32_t) __asm__("revise_integer_division_paths::short_and_skip");
extern int32_t short_or_skip(int32_t) __asm__("revise_integer_division_paths::short_or_skip");
extern int32_t short_and_eval(int32_t) __asm__("revise_integer_division_paths::short_and_eval");
extern int32_t short_or_eval(int32_t) __asm__("revise_integer_division_paths::short_or_eval");
extern int32_t branch_value(int32_t) __asm__("revise_integer_division_paths::branch_value");
extern int32_t branch_skip(int32_t) __asm__("revise_integer_division_paths::branch_skip");
extern int32_t picked_value(int32_t) __asm__("revise_integer_division_paths::picked_value");
extern int32_t loop_guard(int32_t) __asm__("revise_integer_division_paths::loop_guard");
extern int32_t implicit_remainder(int32_t) __asm__("revise_integer_division_paths::implicit_remainder");
extern int32_t array_uint8_remainder(int32_t) __asm__("revise_integer_division_paths::array_uint8_remainder");
extern int32_t nested_arithmetic(int32_t) __asm__("revise_integer_division_paths::nested_arithmetic");
extern int32_t picked_remainder(int32_t) __asm__("revise_integer_division_paths::picked_remainder");
extern int32_t branch_remainder(int32_t) __asm__("revise_integer_division_paths::branch_remainder");
extern int32_t array_store(int32_t) __asm__("revise_integer_division_paths::array_store");
extern int32_t field_store(int32_t) __asm__("revise_integer_division_paths::field_store");

#if defined(_WIN32)
extern int32_t span_div(claw_slice *, claw_slice *, int32_t) __asm__("revise_integer_division_paths::span_div");
extern int32_t span_rem(claw_slice *, claw_slice *, int32_t) __asm__("revise_integer_division_paths::span_rem");
extern int32_t span_min_div(claw_slice *, claw_slice *, int32_t) __asm__("revise_integer_division_paths::span_min_div");
extern int32_t span_min_rem(claw_slice *, claw_slice *, int32_t) __asm__("revise_integer_division_paths::span_min_rem");
#else
extern int32_t span_div(claw_slice, claw_slice, int32_t) __asm__("revise_integer_division_paths::span_div");
extern int32_t span_rem(claw_slice, claw_slice, int32_t) __asm__("revise_integer_division_paths::span_rem");
extern int32_t span_min_div(claw_slice, claw_slice, int32_t) __asm__("revise_integer_division_paths::span_min_div");
extern int32_t span_min_rem(claw_slice, claw_slice, int32_t) __asm__("revise_integer_division_paths::span_min_rem");
#endif

static int32_t call_span(int which, int32_t mode, int32_t *value, int32_t *counter) {
    claw_slice values = {value, 1};
    claw_slice counts = {counter, 1};
#if defined(_WIN32)
    if (which == 14) return span_div(&values, &counts, mode);
    if (which == 15) return span_rem(&values, &counts, mode);
    if (which == 16) return span_min_div(&values, &counts, mode);
    return span_min_rem(&values, &counts, mode);
#else
    if (which == 14) return span_div(values, counts, mode);
    if (which == 15) return span_rem(values, counts, mode);
    if (which == 16) return span_min_div(values, counts, mode);
    return span_min_rem(values, counts, mode);
#endif
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--list") == 0) {
        puts("0 trap");
        puts("1 trap");
        puts("2 trap");
        puts("3 trap");
        puts("4 trap");
        puts("5 trap");
        puts("6 trap");
        puts("7 trap");
        puts("8 trap");
        puts("9 trap");
        puts("10 trap");
        puts("11 trap");
        puts("12 safe");
        puts("13 safe");
        puts("14 trap");
        puts("15 trap");
        puts("16 safe");
        puts("17 safe");
        puts("18 trap");
        puts("19 trap");
        puts("20 safe");
        puts("21 safe");
        puts("22 trap");
        puts("23 trap");
        puts("24 trap");
        puts("25 safe");
        puts("26 trap");
        puts("27 trap");
        puts("28 trap");
        puts("29 trap");
        puts("30 trap");
        puts("31 trap");
        puts("32 trap");
        puts("33 trap");
        puts("34 trap");
        return 0;
    }
    if (argc < 3) return 2;
    int which = atoi(argv[1]);
    int32_t mode = (int32_t)atoi(argv[2]);
    int32_t got = 0;
    int32_t expected = 1;
    switch (which) {
        case 0: got = implicit_tail(mode); expected = 4; break;
        case 1: got = argument_nesting(mode); expected = 9; break;
        case 2: got = if_selected_first(mode); expected = 9; break;
        case 3: got = pick_selected_first(mode); expected = 9; break;
        case 4: got = and_or_first(mode); expected = 9; break;
        case 5: got = loop_paths(mode); expected = 9; break;
        case 6: got = loop_condition(mode); expected = 9; break;
        case 7: got = scan_body(mode); expected = 18; break;
        case 8: got = tuple_projection(mode); expected = 9; break;
        case 9: got = shape_field(mode); expected = 9; break;
        case 10: got = array_int32(mode); expected = 9; break;
        case 11: got = array_uint8(mode); expected = 9; break;
        case 12: got = min_binary(mode); expected = MIN32; break;
        case 13: got = min_remainder(mode); expected = 0; break;
        case 14: case 15: {
            int32_t value = (which == 14) ? 18 : 19;
            int32_t counter = 0;
            got = call_span(which, mode, &value, &counter);
            expected = 1;
            break;
        }
        case 16: case 17: {
            int32_t value = MIN32;
            int32_t counter = 0;
            got = call_span(which, mode, &value, &counter);
            expected = 1;
            break;
        }
        case 18: got = if_selected_other(mode); expected = 9; break;
        case 19: got = pick_selected_other(mode); expected = 9; break;
        case 20: got = short_and_skip(mode); break;
        case 21: got = short_or_skip(mode); break;
        case 22: got = short_and_eval(mode); break;
        case 23: got = short_or_eval(mode); break;
        case 24: got = branch_value(mode); break;
        case 25: got = branch_skip(mode); break;
        case 26: got = picked_value(mode); break;
        case 27: got = loop_guard(mode); break;
        case 28: got = implicit_remainder(mode); break;
        case 29: got = array_uint8_remainder(mode); break;
        case 30: got = nested_arithmetic(mode); break;
        case 31: got = picked_remainder(mode); break;
        case 32: got = branch_remainder(mode); break;
        case 33: got = array_store(mode); break;
        case 34: got = field_store(mode); break;
        default: return 2;
    }
    if (got != expected) return 1;
    puts("1");
    return 0;
}
