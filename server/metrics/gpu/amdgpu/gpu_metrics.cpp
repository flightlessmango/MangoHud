#include <spdlog/spdlog.h>
#include "gpu_metrics.hpp"

AMDGPUMetricsBase::AMDGPUMetricsBase(const std::string& drm_node) : drm_node(drm_node) {

}

bool AMDGPUMetricsBase::setup() {
    // const std::string metrics_path = "/home/user/Desktop/projects/MangoHud/tests/gpu_metrics";
    const std::string metrics_path = "/sys/class/drm/" + drm_node + "/device/gpu_metrics";
    ifs_gpu_metrics.open(metrics_path, std::ios::binary);

    if (!ifs_gpu_metrics.is_open()) {
        SPDLOG_WARN("Failed to open {}", metrics_path);
        return false;
    }

    if (!verify_metrics()) {
        SPDLOG_WARN("Invalid gpu_metrics file {}", metrics_path);
        return false;
    }

    return true;
}

void AMDGPUMetricsBase::poll() {
    static std::vector<char> buf(max(sizeof(gpu_metrics_v1_3), sizeof(gpu_metrics_v2_4)));
    const metrics_table_header* header = reinterpret_cast<metrics_table_header*>(buf.data());

    if (!ifs_gpu_metrics.is_open()) return;
    if (!ifs_gpu_metrics.rdbuf()) return;
    ifs_gpu_metrics.clear();
    ifs_gpu_metrics.seekg(0);
    ifs_gpu_metrics.read(reinterpret_cast<char*>(buf.data()), buf.size());
    size_t bytes_read = ifs_gpu_metrics.gcount();

    if (bytes_read < sizeof(metrics_table_header)) {
        SPDLOG_DEBUG("Failed to read metrics header");
        return;
    }

    uint64_t indep_throttle_status = 0;

    if (header->format_revision == 1) {
        // Desktop GPUs
        const gpu_metrics_v1_3* amdgpu_metrics = reinterpret_cast<gpu_metrics_v1_3*>(buf.data());

        parse_metrics_v1_3(amdgpu_metrics);

        indep_throttle_status = amdgpu_metrics->indep_throttle_status;

        // RDNA 3 almost always shows the TEMP_HOTSPOT throtting flag,
        // so clear that bit
        indep_throttle_status &= ~(1ull << TEMP_HOTSPOT_BIT);
    } else if (header->format_revision == 2) {
        // APUs
        const gpu_metrics_v2_3* amdgpu_metrics = reinterpret_cast<gpu_metrics_v2_3*>(buf.data());

        parse_metrics_v2_3(amdgpu_metrics, header->content_revision);

        if(header->content_revision >= 2)
            indep_throttle_status = amdgpu_metrics->indep_throttle_status;
    }

    /* Throttling: See
    https://elixir.bootlin.com/linux/latest/source/drivers/gpu/drm/amd/pm/swsmu/inc/amdgpu_smu.h
    for the offsets */
    metrics.is_power_throttled      = ((indep_throttle_status >> 0)  & 0xFF  ) != 0;
    metrics.is_current_throttled    = ((indep_throttle_status >> 16) & 0xFF  ) != 0;
    metrics.is_temp_throttled       = ((indep_throttle_status >> 32) & 0xFFFF) != 0;
    metrics.is_other_throttled      = ((indep_throttle_status >> 56) & 0xFF  ) != 0;
}

bool AMDGPUMetricsBase::is_apu() const {
    return _is_apu;
}

bool AMDGPUMetricsBase::verify_metrics() {
    metrics_table_header header = {};

    ifs_gpu_metrics.read(reinterpret_cast<char*>(&header), sizeof(header));

    if (ifs_gpu_metrics.tellg() != sizeof(header)) {
        SPDLOG_DEBUG("Failed to read the metrics header of node '{}'", drm_node);
        return false;
    }

    SPDLOG_DEBUG("gpu_metrics version: {}.{}", header.format_revision, header.content_revision);

    switch (header.format_revision) {
        case 1: // v1_1, v1_2, v1_3
            // v1_0, not naturally aligned
            if(header.content_revision == 0 || header.content_revision > 3)
                break;

            return true;

        case 2: // v2_1, v2_2, v2_3, v2_4
            // v2_0, not naturally aligned
            if(header.content_revision == 0 || header.content_revision > 4)
                break;

            _is_apu = true;

            return true;

        default:
            break;
    }

    SPDLOG_WARN(
        "Unsupported gpu_metrics version: {}.{}", header.format_revision, header.content_revision
    );

    return false;
}

void AMDGPUMetricsBase::parse_metrics_v1_3(const gpu_metrics_v1_3* in) {
    metrics.gpu_load_percent    = in->average_gfx_activity;

    metrics.average_gfx_power_w = in->average_socket_power;

    metrics.current_gfxclk_mhz  = in->current_gfxclk;
    metrics.current_uclk_mhz    = in->current_uclk;

    metrics.gpu_temp_c          = in->temperature_edge;
    metrics.fan_speed           = in->current_fan_speed;
}

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

void AMDGPUMetricsBase::parse_metrics_v2_3(const gpu_metrics_v2_3* in, uint8_t content_revision) {
    metrics.gpu_load_percent = in->average_gfx_activity;
    metrics.average_gfx_power_w = in->average_gfx_power / 1000.f;

    if (IS_VALID_METRIC(in->average_cpu_power)) {
        metrics.average_cpu_power_w = in->average_cpu_power / 1000.f;
    } else if (IS_VALID_METRIC(in->average_core_power[0])) {
        // fallback: sum of core power
        uint8_t i = 0;

        do {
            metrics.average_cpu_power_w = metrics.average_cpu_power_w + in->average_core_power[i] / 1000.f;
        } while (++i < ARRAY_SIZE(in->average_core_power) && IS_VALID_METRIC(in->average_core_power[i]));
    }

    if (IS_VALID_METRIC(in->temperature_gfx))
        metrics.gpu_temp_c = in->temperature_gfx / 100;
    else if (content_revision >= 3 && IS_VALID_METRIC(in->average_temperature_gfx))
        metrics.gpu_temp_c = in->average_temperature_gfx / 100;

    uint16_t cpu_temp = 0;

    if( IS_VALID_METRIC(in->temperature_core[0]) ) {
        uint8_t i = 0;

        do {
            cpu_temp = max(cpu_temp, in->temperature_core[i]);
        } while (++i < ARRAY_SIZE(in->temperature_core) && IS_VALID_METRIC(in->temperature_core[i]));

        metrics.apu_cpu_temp_c = cpu_temp / 100;
    } else if (content_revision >= 3 && IS_VALID_METRIC(in->average_temperature_core[0]) ) {
        uint8_t i = 0;

        do {
            cpu_temp = max(cpu_temp, in->average_temperature_core[i]);
        } while (++i < ARRAY_SIZE(in->average_temperature_core) && IS_VALID_METRIC(in->average_temperature_core[i]));

        metrics.apu_cpu_temp_c = cpu_temp / 100;
    }/* else if( cpuStats.ReadcpuTempFile(cpu_temp) ) {
    // fallback 2: Try temp from file 'm_cpuTempFile' of 'cpu.cpp'
    metrics.apu_cpu_temp_c = cpu_temp;
    }*/

    if (IS_VALID_METRIC(in->current_gfxclk))
        metrics.current_gfxclk_mhz = in->current_gfxclk;
    else if (IS_VALID_METRIC(in->average_gfxclk_frequency))
        metrics.current_gfxclk_mhz = in->average_gfxclk_frequency;

    if (IS_VALID_METRIC(in->current_uclk))
        metrics.current_uclk_mhz = in->current_uclk;
    else if (IS_VALID_METRIC(in->average_uclk_frequency))
        metrics.current_uclk_mhz = in->average_uclk_frequency;
}

#undef ARRAY_SIZE

AMDGPUMetrics::AMDGPUMetrics(const std::string& drm_node)
    : gpu_metrics(AMDGPUMetricsBase(drm_node)) {}
