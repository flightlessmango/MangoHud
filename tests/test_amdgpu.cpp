#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
extern "C" {
#include <cmocka.h>
}
#include "stdio.h"
#include "../src/amdgpu.h"
#include "../src/cpu.h"
#include <endian.h>

#define UNUSED(x) (void)(x)

static void test_amdgpu_verify_metrics(void **state) {
    UNUSED(state);

    assert_false(amdgpu_verify_metrics("./missing_file"));
    // unsupported struct size, format and content revision
    assert_false(amdgpu_verify_metrics("./gpu_metrics_invalid"));
    assert_true (amdgpu_verify_metrics("./gpu_metrics"));
}

static void test_amdgpu_get_instant_metrics(void **state) {
    UNUSED(state);
    struct amdgpu_common_metrics metrics;

    // fail fetch gpu_metrics file
    metrics_path = "./missing_file";
    amdgpu_get_instant_metrics(&metrics);

    // DGPU
    metrics_path = "./gpu_metrics";
    metrics = {};
    amdgpu_get_instant_metrics(&metrics);
    assert_int_equal(metrics.gpu_load_percent, 64);
    assert_float_equal(metrics.average_gfx_power_w, 33, 0);
    assert_float_equal(metrics.average_cpu_power_w, 0, 0);
    assert_int_equal(metrics.current_gfxclk_mhz, 2165);
    assert_int_equal(metrics.current_uclk_mhz, 1000);
    assert_int_equal(metrics.gpu_temp_c, 36);
    assert_int_equal(metrics.soc_temp_c, 0);
    assert_int_equal(metrics.apu_cpu_temp_c, 0);
    assert_false(metrics.is_power_throttled);
    assert_false(metrics.is_current_throttled);
    assert_false(metrics.is_temp_throttled);
    assert_false(metrics.is_other_throttled);

    // DGPU
    metrics_path = "./gpu_metrics_reserved_throttle_bits";
    metrics = {};
    amdgpu_get_instant_metrics(&metrics);
    assert_false(metrics.is_power_throttled);
    assert_false(metrics.is_current_throttled);
    assert_false(metrics.is_temp_throttled);
    assert_false(metrics.is_other_throttled);

    metrics_path = "./gpu_metrics_apu";
    metrics = {};
    amdgpu_get_instant_metrics(&metrics);
    assert_int_equal(metrics.gpu_load_percent, 100);
    assert_float_equal(metrics.average_gfx_power_w, 6.161, 0);
    assert_float_equal(metrics.average_cpu_power_w, 9.235, 0);
    assert_int_equal(metrics.current_gfxclk_mhz, 1040);
    assert_int_equal(metrics.current_uclk_mhz, 687);
    assert_int_equal(metrics.gpu_temp_c, 81);
    assert_int_equal(metrics.soc_temp_c, 71);
    assert_int_equal(metrics.apu_cpu_temp_c, 80);
    assert_true(metrics.is_power_throttled);
    assert_false(metrics.is_current_throttled);
    assert_false(metrics.is_temp_throttled);
    assert_false(metrics.is_other_throttled);
    // amdgpu binary with everything throttled
}

static void test_amdgpu_get_samples_and_copy(void **state) {
    UNUSED(state);

    struct amdgpu_common_metrics metrics_buffer[100];
    bool gpu_load_needs_dividing = false;  //some GPUs report load as centipercent
    amdgpu_get_samples_and_copy(metrics_buffer, gpu_load_needs_dividing);
    gpu_load_needs_dividing = true;
    amdgpu_get_samples_and_copy(metrics_buffer, gpu_load_needs_dividing);
}

static void test_amdgpu_get_metrics(void **state) {
    UNUSED(state);

    amdgpu_get_metrics(0x1435);
}

const struct CMUnitTest amdgpu_tests[] = {
    cmocka_unit_test(test_amdgpu_verify_metrics),
    cmocka_unit_test(test_amdgpu_get_instant_metrics),
    cmocka_unit_test(test_amdgpu_get_samples_and_copy),
    cmocka_unit_test(test_amdgpu_get_metrics)
};

int main(void) {
    return cmocka_run_group_tests(amdgpu_tests, NULL, NULL);
}