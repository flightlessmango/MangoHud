#include "cpu.h"
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <sys/cpuctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <vector>
#include <spdlog/spdlog.h>
#include "sysctl_utils.h"

// Per-CPU tick counters, CPUSTATES longs per CPU id:
// CP_USER, CP_NICE, CP_SYS, CP_INTR, CP_IDLE. Slots of absent CPUs are zero.
static bool read_cp_times(std::vector<long>& out)
{
    size_t len = 0;

    if (sysctlbyname("kern.cp_times", nullptr, &len, nullptr, 0) != 0
        || len < CPUSTATES * sizeof(long))
        return false;

    out.resize(len / sizeof(long));
    len = out.size() * sizeof(long);
    return sysctlbyname("kern.cp_times", out.data(), &len, nullptr, 0) == 0;
}

static void calculate(CPUData& cpu, const long* t)
{
    calculateCPUData(cpu,
        t[CP_USER], t[CP_NICE], t[CP_SYS], t[CP_IDLE],
        0 /* iowait */, t[CP_INTR], 0 /* softirq */, 0 /* steal */, 0 /* guest */, 0 /* guestnice */);
}

bool CPUStats::Init()
{
    if (m_inited)
        return true;

    std::vector<long> times;
    if (!read_cp_times(times)) {
        SPDLOG_ERROR("Failed to query kern.cp_times");
        return false;
    }

    m_cpuData.clear();
    for (size_t i = 0; i < times.size() / CPUSTATES; i++) {
        const long* t = &times[i * CPUSTATES];
        if (!(t[CP_USER] | t[CP_NICE] | t[CP_SYS] | t[CP_INTR] | t[CP_IDLE]))
            continue; // no such CPU

        CPUData cpu = {};
        cpu.totalTime = 1;
        cpu.totalPeriod = 1;
        cpu.cpu_id = static_cast<int>(i);
        m_cpuData.push_back(cpu);
    }

    if (m_cpuData.empty())
        return false;

    m_inited = true;
    return UpdateCPUData();
}

bool CPUStats::UpdateCPUData()
{
    if (!m_inited)
        return false;

    long total[CPUSTATES];
    size_t len = sizeof(total);
    if (sysctlbyname("kern.cp_time", total, &len, nullptr, 0) != 0)
        return false;
    calculate(m_cpuDataTotal, total);

    std::vector<long> times;
    if (!read_cp_times(times))
        return false;

    for (auto& cpu : m_cpuData) {
        if ((static_cast<size_t>(cpu.cpu_id) + 1) * CPUSTATES > times.size())
            continue;
        calculate(cpu, &times[cpu.cpu_id * CPUSTATES]);
    }

    m_cpuDataTotal.max_core_load = 0;
    for (const auto& data : m_cpuData)
        if (data.percent > m_cpuDataTotal.max_core_load)
            m_cpuDataTotal.max_core_load = data.percent;

    m_cpuPeriod = static_cast<double>(m_cpuData[0].totalPeriod) / m_cpuData.size();
    m_updatedCPUs = true;
    return true;
}

// The selected P-state of cpufreq(4), not a measured clock. Some drivers only
// provide dev.cpu.0.freq, the other cores show 0.
bool CPUStats::UpdateCoreMhz()
{
    m_coreMhz.clear();

    for (auto& cpu : m_cpuData) {
        int mhz = 0;
        const std::string name = "dev.cpu." + std::to_string(cpu.cpu_id) + ".freq";
        if (!sysctl_read(name.c_str(), mhz) || mhz < 0)
            mhz = 0;
        cpu.mhz = mhz;
    }

    m_cpuDataTotal.cpu_mhz = 0;
    for (const auto& data : m_cpuData)
        if (data.mhz > m_cpuDataTotal.cpu_mhz)
            m_cpuDataTotal.cpu_mhz = data.mhz;

    return true;
}

// coretemp(4) and amdtemp(4) report dev.cpu.N.temperature in deci-Kelvin
bool CPUStats::ReadcpuTempFile(int& temp)
{
    long sum = 0, count = 0;

    for (const auto& cpu : m_cpuData) {
        int deci_kelvin = 0;
        const std::string name = "dev.cpu." + std::to_string(cpu.cpu_id) + ".temperature";
        if (!sysctl_read(name.c_str(), deci_kelvin) || deci_kelvin <= 0)
            continue; // no sensor on this CPU

        sum += deci_kelvin - 2731;
        count++;
    }

    if (!count)
        return false;

    temp = static_cast<int>(sum / (10 * count));
    return true;
}

bool CPUStats::UpdateCpuTemp()
{
    int temp = 0;
    bool ret = ReadcpuTempFile(temp);
    m_cpuDataTotal.temp = temp;

    return ret;
}

// Usable if coretemp(4) or amdtemp(4) provides a sensor
bool CPUStats::GetCpuFile()
{
    int temp = 0;
    return ReadcpuTempFile(temp);
}

bool CPUStats::UpdatePlatformFanSpeed()
{
    m_platformFanSpeed = -1;
    return false;
}

// Package power from the RAPL energy counter, read through cpuctl(4):
//   Intel: MSR_RAPL_POWER_UNIT 0x606, MSR_PKG_ENERGY_STATUS 0x611
//   AMD:   MSR_AMD_RAPL_POWER_UNIT 0xc0010299, MSR_AMD_PKG_ENERGY_STATUS 0xc001029b
// Energy unit is 1 / 2^((unit >> 8) & 0x1f) joules, the counter is 32 bits wide.
// Needs `kldload cpuctl` and read access to /dev/cpuctl0.
struct CPUPowerData_cpuctl : public CPUPowerData {
   CPUPowerData_cpuctl() {
      this->source = CPU_POWER_CPUCTL;
   };

   ~CPUPowerData_cpuctl() {
      if (this->fd >= 0)
         close(this->fd);
   };

   int fd {-1};
   uint32_t energy_msr {0};
   double joules_per_count {0};
   uint32_t lastCounterValue {0};
   bool haveLast {false};
   Clock::time_point lastCounterValueTime;
};

static bool cpuctl_rdmsr(int fd, uint32_t msr, uint64_t& out)
{
    cpuctl_msr_args_t args;
    args.msr = static_cast<int>(msr); // the kernel reads it back as unsigned
    args.data = 0;

    if (ioctl(fd, CPUCTL_RDMSR, &args) != 0)
        return false; // MSR not supported on this CPU
    out = args.data;
    return true;
}

static bool get_cpu_power_cpuctl(CPUPowerData_cpuctl* pd, float& power)
{
    uint64_t raw = 0;
    if (pd->fd < 0 || !cpuctl_rdmsr(pd->fd, pd->energy_msr, raw))
        return false;

    const uint32_t counter = static_cast<uint32_t>(raw);
    const Clock::time_point now = Clock::now();

    if (pd->haveLast) {
        const double dt = std::chrono::duration<double>(now - pd->lastCounterValueTime).count();
        if (dt < 0.1)
            return false; // too soon for a meaningful reading, keep the old value

        const uint32_t diff = counter - pd->lastCounterValue; // wraps correctly
        power = static_cast<float>(diff * pd->joules_per_count / dt);
    }

    pd->lastCounterValue = counter;
    pd->lastCounterValueTime = now;
    pd->haveLast = true;
    return true;
}

static CPUPowerData_cpuctl* init_cpu_power_data_cpuctl()
{
    int fd = open("/dev/cpuctl0", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        SPDLOG_WARN("cpu_power: can't open /dev/cpuctl0 ({}); load cpuctl(4) and make the device readable", strerror(errno));
        return nullptr;
    }

    struct Candidate { uint32_t unit_msr; uint32_t energy_msr; };
    const Candidate amd   = { 0xc0010299, 0xc001029b };
    const Candidate intel = { 0x606, 0x611 };

    // try the CPU's own vendor first
    const bool is_intel = sysctl_read_string("hw.model").find("Intel") != std::string::npos;
    const Candidate order[2] = { is_intel ? intel : amd, is_intel ? amd : intel };

    for (const auto& c : order) {
        uint64_t unit = 0, energy = 0;
        if (!cpuctl_rdmsr(fd, c.unit_msr, unit) || !cpuctl_rdmsr(fd, c.energy_msr, energy))
            continue;

        const unsigned esu = (unit >> 8) & 0x1f;
        if (esu == 0)
            continue; // implausible, not a RAPL unit register

        auto powerData = std::make_unique<CPUPowerData_cpuctl>();
        powerData->fd = fd;
        powerData->energy_msr = c.energy_msr;
        powerData->joules_per_count = std::ldexp(1.0, -static_cast<int>(esu));
        SPDLOG_DEBUG("cpu_power: using MSR {:#x}, {} joules per count", c.energy_msr, powerData->joules_per_count);
        return powerData.release();
    }

    SPDLOG_WARN("cpu_power: no readable package energy MSR on this CPU");
    close(fd);
    return nullptr;
}

bool CPUStats::InitCpuPowerData()
{
    static bool tried = false;

    if (m_cpuPowerData)
        return true;
    if (tried)
        return false;
    tried = true;

    CPUPowerData_cpuctl* powerData = init_cpu_power_data_cpuctl();
    if (!powerData)
        return false;

    m_cpuPowerData.reset(powerData);
    return true;
}

bool CPUStats::UpdateCpuPower()
{
    if (!InitCpuPowerData())
        return false;

    float power = m_cpuDataTotal.power; // keep the last value on a skipped sample
    if (!get_cpu_power_cpuctl(static_cast<CPUPowerData_cpuctl*>(m_cpuPowerData.get()), power))
        return false;

    m_cpuDataTotal.power = power;
    return true;
}
