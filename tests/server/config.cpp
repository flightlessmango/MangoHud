// #include <cstddef>
// #include <cstdarg>
// extern "C" {
//     #include <setjmp.h>
//     #include <cmocka.h>
// }

// #ifdef fail
//   #undef fail
// #endif

// #include "../../server/config.h"

// static void parse_config(void **state) {
//     HudTable t;
//     assert_false(parse_hud_table(t, "badfile"));
//     t = HudTable {};
//     assert_false(parse_hud_table(t, "tests/server/assets/malformed.yml"));
//     t = HudTable {};
//     assert_true(parse_hud_table(t, "tests/server/assets/MangoHud.yml"));
//     assert_true(t.cols == 3);
//     assert_true(int(t.rows.size()) == 7);
// }

// int main(void) {
//     const struct CMUnitTest tests[] = {
//         cmocka_unit_test(parse_config),
//     };
//     return cmocka_run_group_tests(tests, nullptr, nullptr);
// }
