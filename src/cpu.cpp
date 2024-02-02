#include "cpu.h"
#include <memory>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <string.h>
#include <algorithm>
#include <regex>
#include <inttypes.h>
#include <spdlog/spdlog.h>
#include "string_utils.h"
#include "gpu.h"

#ifndef PROCDIR
#define PROCDIR "/proc"
#endif

#ifndef PROCSTATFILE
#define PROCSTATFILE PROCDIR "/stat"
#endif

#ifndef PROCMEMINFOFILE
#define PROCMEMINFOFILE PROCDIR "/meminfo"
#endif

#ifndef PROCCPUINFOFILE
#define PROCCPUINFOFILE PROCDIR "/cpuinfo"
#endif

#include "file_utils.h"

static void calculateCPUData(CPUData& cpuData,
    unsigned long long int usertime,
    unsigned long long int nicetime,
    unsigned long long int systemtime,
    unsigned long long int idletime,
    unsigned long long int ioWait,
    unsigned long long int irq,
    unsigned long long int softIrq,
    unsigned long long int steal,
    unsigned long long int guest,
    unsigned long long int guestnice)
{
    // Guest time is already accounted in usertime
    usertime = usertime - guest;
    nicetime = nicetime - guestnice;
    // Fields existing on kernels >= 2.6
    // (and RHEL's patched kernel 2.4...)
    unsigned long long int idlealltime = idletime + ioWait;
    unsigned long long int systemalltime = systemtime + irq + softIrq;
    unsigned long long int virtalltime = guest + guestnice;
    unsigned long long int totaltime = usertime + nicetime + systemalltime + idlealltime + steal + virtalltime;

    // Since we do a subtraction (usertime - guest) and cputime64_to_clock_t()
    // used in /proc/stat rounds down numbers, it can lead to a case where the
    // integer overflow.
    #define WRAP_SUBTRACT(a,b) (a > b) ? a - b : 0
    cpuData.userPeriod = WRAP_SUBTRACT(usertime, cpuData.userTime);
    cpuData.nicePeriod = WRAP_SUBTRACT(nicetime, cpuData.niceTime);
    cpuData.systemPeriod = WRAP_SUBTRACT(systemtime, cpuData.systemTime);
    cpuData.systemAllPeriod = WRAP_SUBTRACT(systemalltime, cpuData.systemAllTime);
    cpuData.idleAllPeriod = WRAP_SUBTRACT(idlealltime, cpuData.idleAllTime);
    cpuData.idlePeriod = WRAP_SUBTRACT(idletime, cpuData.idleTime);
    cpuData.ioWaitPeriod = WRAP_SUBTRACT(ioWait, cpuData.ioWaitTime);
    cpuData.irqPeriod = WRAP_SUBTRACT(irq, cpuData.irqTime);
    cpuData.softIrqPeriod = WRAP_SUBTRACT(softIrq, cpuData.softIrqTime);
    cpuData.stealPeriod = WRAP_SUBTRACT(steal, cpuData.stealTime);
    cpuData.guestPeriod = WRAP_SUBTRACT(virtalltime, cpuData.guestTime);
    cpuData.totalPeriod = WRAP_SUBTRACT(totaltime, cpuData.totalTime);
    #undef WRAP_SUBTRACT
    cpuData.userTime = usertime;
    cpuData.niceTime = nicetime;
    cpuData.systemTime = systemtime;
    cpuData.systemAllTime = systemalltime;
    cpuData.idleAllTime = idlealltime;
    cpuData.idleTime = idletime;
    cpuData.ioWaitTime = ioWait;
    cpuData.irqTime = irq;
    cpuData.softIrqTime = softIrq;
    cpuData.stealTime = steal;
    cpuData.guestTime = virtalltime;
    cpuData.totalTime = totaltime;

    if (cpuData.totalPeriod == 0)
        return;
    float total = (float)cpuData.totalPeriod;
    float v[4];
    v[0] = cpuData.nicePeriod * 100.0f / total;
    v[1] = cpuData.userPeriod * 100.0f / total;

    /* if not detailed */
    v[2] = cpuData.systemAllPeriod * 100.0f / total;
    v[3] = (cpuData.stealPeriod + cpuData.guestPeriod) * 100.0f / total;
    //cpuData.percent = std::clamp(v[0]+v[1]+v[2]+v[3], 0.0f, 100.0f);
    cpuData.percent = std::min(std::max(v[0]+v[1]+v[2]+v[3], 0.0f), 100.0f);
}

CPUStats::CPUStats()
{
}

CPUStats::~CPUStats()
{
    if (m_cpuTempFile)
        fclose(m_cpuTempFile);
}

bool CPUStats::Init()
{
    if (m_inited)
        return true;

    std::string line;
    std::ifstream file (PROCSTATFILE);
    bool first = true;
    m_cpuData.clear();

    if (!file.is_open()) {
        SPDLOG_ERROR("Failed to opening " PROCSTATFILE);
        return false;
    }

    do {
        if (!std::getline(file, line)) {
            SPDLOG_DEBUG("Failed to read all of " PROCSTATFILE);
            return false;
        } else if (starts_with(line, "cpu")) {
            if (first) {
                first =false;
                continue;
            }

            CPUData cpu = {};
            cpu.totalTime = 1;
            cpu.totalPeriod = 1;
            sscanf(line.c_str(), "cpu%4d ", &cpu.cpu_id);
            m_cpuData.push_back(cpu);

        } else if (starts_with(line, "btime ")) {

            // C++ way, kind of noisy
            //std::istringstream token( line );
            //std::string s;
            //token >> s;
            //token >> m_boottime;

            // assume that if btime got read, that everything else is OK too
            sscanf(line.c_str(), "btime %lld\n", &m_boottime);
            break;
        }
    } while(true);

    m_inited = true;
    return UpdateCPUData();
}

bool CPUStats::Reinit()
{
    m_inited = false;
    return Init();
}

//TODO take sampling interval into account?
bool CPUStats::UpdateCPUData()
{
    unsigned long long int usertime, nicetime, systemtime, idletime;
    unsigned long long int ioWait, irq, softIrq, steal, guest, guestnice;
    int cpuid = -1;
    size_t cpu_count = 0;

    if (!m_inited)
        return false;

    std::string line;
    std::ifstream file (PROCSTATFILE);
    bool ret = false;

    if (!file.is_open()) {
        SPDLOG_ERROR("Failed to opening " PROCSTATFILE);
        return false;
    }

    do {
        if (!std::getline(file, line)) {
            break;
        } else if (!ret && sscanf(line.c_str(), "cpu  %16llu %16llu %16llu %16llu %16llu %16llu %16llu %16llu %16llu %16llu",
            &usertime, &nicetime, &systemtime, &idletime, &ioWait, &irq, &softIrq, &steal, &guest, &guestnice) == 10) {
            ret = true;
            calculateCPUData(m_cpuDataTotal, usertime, nicetime, systemtime, idletime, ioWait, irq, softIrq, steal, guest, guestnice);
        } else if (sscanf(line.c_str(), "cpu%4d %16llu %16llu %16llu %16llu %16llu %16llu %16llu %16llu %16llu %16llu",
            &cpuid, &usertime, &nicetime, &systemtime, &idletime, &ioWait, &irq, &softIrq, &steal, &guest, &guestnice) == 11) {

            //SPDLOG_DEBUG("Parsing 'cpu{}' line:{}", cpuid, line);

            if (!ret) {
                SPDLOG_DEBUG("Failed to parse 'cpu' line:{}", line);
                return false;
            }

            if (cpuid < 0 /* can it? */) {
                SPDLOG_DEBUG("Cpu id '{}' is out of bounds", cpuid);
                return false;
            }

            if (cpu_count + 1 > m_cpuData.size() || m_cpuData[cpu_count].cpu_id != cpuid) {
                SPDLOG_DEBUG("Cpu id '{}' is out of bounds or wrong index, reiniting", cpuid);
                return Reinit();
            }

            CPUData& cpuData = m_cpuData[cpu_count];
            calculateCPUData(cpuData, usertime, nicetime, systemtime, idletime, ioWait, irq, softIrq, steal, guest, guestnice);
            cpuid = -1;
            cpu_count++;

        } else {
            break;
        }
    } while(true);

    if (cpu_count < m_cpuData.size())
        m_cpuData.resize(cpu_count);

    m_cpuPeriod = (double)m_cpuData[0].totalPeriod / m_cpuData.size();
    m_updatedCPUs = true;
    return ret;
}

bool CPUStats::UpdateCoreMhz() {
    m_coreMhz.clear();
    FILE *fp;
    static bool scaling_freq = true;
    if (scaling_freq){
        for (auto& cpu : m_cpuData){
            std::string path = "/sys/devices/system/cpu/cpu" + std::to_string(cpu.cpu_id) + "/cpufreq/scaling_cur_freq";
            if ((fp = fopen(path.c_str(), "r"))){
                int64_t temp;
                if (fscanf(fp, "%" PRId64, &temp) != 1)
                    temp = 0;
                cpu.mhz = temp / 1000;
                fclose(fp);
                scaling_freq = true;
            } else {
                scaling_freq = false;
                break;
            }
        }
    } else {
        static std::ifstream cpuInfo(PROCCPUINFOFILE);
        static std::string row;
        size_t i = 0;
        while (std::getline(cpuInfo, row) && i < m_cpuData.size()) {
            if (row.find("MHz") != std::string::npos){
                row = std::regex_replace(row, std::regex(R"([^0-9.])"), "");
                if (!try_stoi(m_cpuData[i].mhz, row))
                    m_cpuData[i].mhz = 0;
                i++;
            }
        }
    }

    m_cpuDataTotal.cpu_mhz = 0;
    for (auto data : m_cpuData)
        if (data.mhz > m_cpuDataTotal.cpu_mhz)
            m_cpuDataTotal.cpu_mhz = data.mhz;

    return true;
}

bool CPUStats::ReadcpuTempFile(int& temp) {
	if (!m_cpuTempFile)
		return false;

	rewind(m_cpuTempFile);
	fflush(m_cpuTempFile);
	bool ret = (fscanf(m_cpuTempFile, "%d", &temp) == 1);
	temp = temp / 1000;

	return ret;
}

bool CPUStats::UpdateCpuTemp() {
	if (cpu_type == "APU"){
        m_cpuDataTotal.temp = gpu_info.apu_cpu_temp;
        return true;
    } else {
        int temp = 0;
		bool ret = ReadcpuTempFile(temp);
		m_cpuDataTotal.temp = temp;

        return ret;
    }
}

static bool get_cpu_power_k10temp(CPUPowerData* cpuPowerData, float& power) {
    CPUPowerData_k10temp* powerData_k10temp = (CPUPowerData_k10temp*)cpuPowerData;

    if(powerData_k10temp->corePowerFile || powerData_k10temp->socPowerFile)
    {
        rewind(powerData_k10temp->corePowerFile);
        rewind(powerData_k10temp->socPowerFile);
        fflush(powerData_k10temp->corePowerFile);
        fflush(powerData_k10temp->socPowerFile);
        int corePower, socPower;
        if (fscanf(powerData_k10temp->corePowerFile, "%d", &corePower) != 1)
            goto voltagebased;
        if (fscanf(powerData_k10temp->socPowerFile, "%d", &socPower) != 1)
            goto voltagebased;
        power = (corePower + socPower) / 1000000;
        return true;
    }
    voltagebased:
    if (!powerData_k10temp->coreVoltageFile || !powerData_k10temp->coreCurrentFile || !powerData_k10temp->socVoltageFile || !powerData_k10temp->socCurrentFile)
        return false;
    rewind(powerData_k10temp->coreVoltageFile);
    rewind(powerData_k10temp->coreCurrentFile);
    rewind(powerData_k10temp->socVoltageFile);
    rewind(powerData_k10temp->socCurrentFile);

    fflush(powerData_k10temp->coreVoltageFile);
    fflush(powerData_k10temp->coreCurrentFile);
    fflush(powerData_k10temp->socVoltageFile);
    fflush(powerData_k10temp->socCurrentFile);

    int coreVoltage, coreCurrent;
    int socVoltage, socCurrent;

    if (fscanf(powerData_k10temp->coreVoltageFile, "%d", &coreVoltage) != 1)
        return false;
    if (fscanf(powerData_k10temp->coreCurrentFile, "%d", &coreCurrent) != 1)
        return false;
    if (fscanf(powerData_k10temp->socVoltageFile, "%d", &socVoltage) != 1)
        return false;
    if (fscanf(powerData_k10temp->socCurrentFile, "%d", &socCurrent) != 1)
        return false;

    power = (coreVoltage * coreCurrent + socVoltage * socCurrent) / 1000000;

    return true;
}

static bool get_cpu_power_zenpower(CPUPowerData* cpuPowerData, float& power) {
    CPUPowerData_zenpower* powerData_zenpower = (CPUPowerData_zenpower*)cpuPowerData;

    if (!powerData_zenpower->corePowerFile || !powerData_zenpower->socPowerFile)
        return false;

    rewind(powerData_zenpower->corePowerFile);
    rewind(powerData_zenpower->socPowerFile);

    fflush(powerData_zenpower->corePowerFile);
    fflush(powerData_zenpower->socPowerFile);

    int corePower, socPower;

    if (fscanf(powerData_zenpower->corePowerFile, "%d", &corePower) != 1)
        return false;
    if (fscanf(powerData_zenpower->socPowerFile, "%d", &socPower) != 1)
        return false;

    power = (corePower + socPower) / 1000000;

    return true;
}

static bool get_cpu_power_zenergy(CPUPowerData* cpuPowerData, float& power) {
    CPUPowerData_zenergy* powerData_zenergy = (CPUPowerData_zenergy*)cpuPowerData;
    if (!powerData_zenergy->energyCounterFile)
        return false;

    rewind(powerData_zenergy->energyCounterFile);
    fflush(powerData_zenergy->energyCounterFile);

    uint64_t energyCounterValue = 0;
    if (fscanf(powerData_zenergy->energyCounterFile, "%" SCNu64, &energyCounterValue) != 1)
        return false;

    Clock::time_point now = Clock::now();
    Clock::duration timeDiff = now - powerData_zenergy->lastCounterValueTime;
    int64_t timeDiffMicro = std::chrono::duration_cast<std::chrono::microseconds>(timeDiff).count();
    uint64_t energyCounterDiff = energyCounterValue - powerData_zenergy->lastCounterValue;


    if (powerData_zenergy->lastCounterValue > 0 && energyCounterValue > powerData_zenergy->lastCounterValue)
        power = (float) energyCounterDiff / (float) timeDiffMicro;

    powerData_zenergy->lastCounterValue = energyCounterValue;
    powerData_zenergy->lastCounterValueTime = now;

    return true;
}

static bool get_cpu_power_rapl(CPUPowerData* cpuPowerData, float& power) {
    CPUPowerData_rapl* powerData_rapl = (CPUPowerData_rapl*)cpuPowerData;

    if (!powerData_rapl->energyCounterFile)
        return false;

    rewind(powerData_rapl->energyCounterFile);
    fflush(powerData_rapl->energyCounterFile);

    uint64_t energyCounterValue = 0;
    if (fscanf(powerData_rapl->energyCounterFile, "%" SCNu64, &energyCounterValue) != 1)
        return false;

    Clock::time_point now = Clock::now();
    Clock::duration timeDiff = now - powerData_rapl->lastCounterValueTime;
    int64_t timeDiffMicro = std::chrono::duration_cast<std::chrono::microseconds>(timeDiff).count();
    uint64_t energyCounterDiff = energyCounterValue - powerData_rapl->lastCounterValue;

    if (powerData_rapl->lastCounterValue > 0 && energyCounterValue > powerData_rapl->lastCounterValue)
        power = energyCounterDiff / timeDiffMicro;

    powerData_rapl->lastCounterValue = energyCounterValue;
    powerData_rapl->lastCounterValueTime = now;

    return true;
}

static bool get_cpu_power_amdgpu(float& power) {
    power = gpu_info.apu_cpu_power;
    return true;
}

bool CPUStats::UpdateCpuPower() {
    if(!m_cpuPowerData)
        return false;

    float power = 0;

    switch(m_cpuPowerData->source) {
        case CPU_POWER_K10TEMP:
            if (!get_cpu_power_k10temp(m_cpuPowerData.get(), power)) return false;
            break;
        case CPU_POWER_ZENPOWER:
            if (!get_cpu_power_zenpower(m_cpuPowerData.get(), power)) return false;
            break;
        case CPU_POWER_ZENERGY:
            if (!get_cpu_power_zenergy(m_cpuPowerData.get(), power)) return false;
            break;
        case CPU_POWER_RAPL:
            if (!get_cpu_power_rapl(m_cpuPowerData.get(), power)) return false;
            break;
        case CPU_POWER_AMDGPU:
            if (!get_cpu_power_amdgpu(power)) return false;
            break;
        default:
            return false;
    }

    m_cpuDataTotal.power = power;

    return true;
}

static bool find_input(const std::string& path, const char* input_prefix, std::string& input, const std::string& name)
{
    auto files = ls(path.c_str(), input_prefix, LS_FILES);
    for (auto& file : files) {
        if (!ends_with(file, "_label"))
            continue;

        auto label = read_line(path + "/" + file);
        if (label != name)
            continue;

        auto uscore = file.find_first_of("_");
        if (uscore != std::string::npos) {
            file.erase(uscore, std::string::npos);
            input = path + "/" + file + "_input";
            return true;
        }
    }
    return false;
}

static bool find_fallback_input(const std::string& path, const char* input_prefix, std::string& input)
{
    auto files = ls(path.c_str(), input_prefix, LS_FILES);
    if (!files.size())
        return false;

    std::sort(files.begin(), files.end());
    for (auto& file : files) {
        if (!ends_with(file, "_input"))
            continue;
        input = path + "/" + file;
		SPDLOG_DEBUG("fallback cpu {} input: {}", input_prefix, input);
        return true;
    }
    return false;
}

bool CPUStats::GetCpuFile() {
    if (m_cpuTempFile)
        return true;

    std::string name, path, input;
    std::string hwmon = "/sys/class/hwmon/";

    auto dirs = ls(hwmon.c_str());
    for (auto& dir : dirs) {
        path = hwmon + dir;
        name = read_line(path + "/name");
        SPDLOG_DEBUG("hwmon: sensor name: {}", name);

        if (name == "coretemp") {
            find_input(path, "temp", input, "Package id 0");
            break;
        }
        else if ((name == "zenpower" || name == "k10temp")) {
            if (!find_input(path, "temp", input, "Tdie"))
                find_input(path, "temp", input, "Tctl");
            break;
        } else if (name == "atk0110") {
            find_input(path, "temp", input, "CPU Temperature");
            break;
        } else if (name == "it8603") {
            find_input(path, "temp", input, "temp1");
            break;
        } else if (starts_with(name, "nct")) {
            // Only break if nct module has TSI0_TEMP node
            if (find_input(path, "temp", input, "TSI0_TEMP"))
                break;

        } else if (name == "asusec") {
            find_input(path, "temp", input, "CPU");
            break;
        } else {
            path.clear();
        }
    }
    if (path.empty() || (!file_exists(input) && !find_fallback_input(path, "temp", input))) {
        SPDLOG_ERROR("Could not find cpu temp sensor location");
        return false;
    } else {
        SPDLOG_DEBUG("hwmon: using input: {}", input);
        m_cpuTempFile = fopen(input.c_str(), "r");
    }
    return true;
}

static CPUPowerData_k10temp* init_cpu_power_data_k10temp(const std::string path) {
    auto powerData = std::make_unique<CPUPowerData_k10temp>();

    std::string coreVoltageInput, coreCurrentInput;
    std::string socVoltageInput, socCurrentInput;
    std::string socPowerInput, corePowerInput;

    if(find_input(path, "power", corePowerInput, "Pcore") && find_input(path, "power", socPowerInput, "Psoc")) {
        powerData->corePowerFile = fopen(corePowerInput.c_str(), "r");
        powerData->socPowerFile = fopen(socPowerInput.c_str(), "r");
        SPDLOG_DEBUG("hwmon: using input: {}", corePowerInput);
        SPDLOG_DEBUG("hwmon: using input: {}", socPowerInput);
        return powerData.release();
    }

    if(!find_input(path, "in", coreVoltageInput, "Vcore")) return nullptr;
    if(!find_input(path, "curr", coreCurrentInput, "Icore")) return nullptr;
    if(!find_input(path, "in", socVoltageInput, "Vsoc")) return nullptr;
    if(!find_input(path, "curr", socCurrentInput, "Isoc")) return nullptr;

    SPDLOG_DEBUG("hwmon: using input: {}", coreVoltageInput);
    SPDLOG_DEBUG("hwmon: using input: {}", coreCurrentInput);
    SPDLOG_DEBUG("hwmon: using input: {}", socVoltageInput);
    SPDLOG_DEBUG("hwmon: using input: {}", socCurrentInput);

    powerData->coreVoltageFile = fopen(coreVoltageInput.c_str(), "r");
    powerData->coreCurrentFile = fopen(coreCurrentInput.c_str(), "r");
    powerData->socVoltageFile = fopen(socVoltageInput.c_str(), "r");
    powerData->socCurrentFile = fopen(socCurrentInput.c_str(), "r");

    return powerData.release();
}

static CPUPowerData_zenpower* init_cpu_power_data_zenpower(const std::string path) {
    auto powerData = std::make_unique<CPUPowerData_zenpower>();

    std::string corePowerInput, socPowerInput;

    if(!find_input(path, "power", corePowerInput, "SVI2_P_Core")) return nullptr;
    if(!find_input(path, "power", socPowerInput, "SVI2_P_SoC")) return nullptr;

    SPDLOG_DEBUG("hwmon: using input: {}", corePowerInput);
    SPDLOG_DEBUG("hwmon: using input: {}", socPowerInput);

    powerData->corePowerFile = fopen(corePowerInput.c_str(), "r");
    powerData->socPowerFile = fopen(socPowerInput.c_str(), "r");

    return powerData.release();
}

static CPUPowerData_zenergy* init_cpu_power_data_zenergy(const std::string path) {
    auto powerData = std::make_unique<CPUPowerData_zenergy>();
    std::string energyCounterPath;

    if(!find_input(path, "energy", energyCounterPath, "Esocket0")) return nullptr;

    SPDLOG_DEBUG("hwmon: using input: {}", energyCounterPath);
    powerData->energyCounterFile = fopen(energyCounterPath.c_str(), "r");

    return powerData.release();
}

static CPUPowerData_rapl* init_cpu_power_data_rapl(const std::string path) {
    auto powerData = std::make_unique<CPUPowerData_rapl>();

    std::string energyCounterPath = path + "/energy_uj";
    if (!file_exists(energyCounterPath)) return nullptr;

    powerData->energyCounterFile = fopen(energyCounterPath.c_str(), "r");

    return powerData.release();
}

bool CPUStats::InitCpuPowerData() {
    if(m_cpuPowerData != nullptr)
        return true;

    std::string name, path;
    std::string hwmon = "/sys/class/hwmon/";
    bool intel = false;

    CPUPowerData* cpuPowerData = nullptr;

    auto dirs = ls(hwmon.c_str());
    for (auto& dir : dirs) {
        path = hwmon + dir;
        name = read_line(path + "/name");
        SPDLOG_DEBUG("hwmon: sensor name: {}", name);

        if (name == "k10temp") {
            cpuPowerData = (CPUPowerData*)init_cpu_power_data_k10temp(path);
        } else if (name == "zenpower") {
            cpuPowerData = (CPUPowerData*)init_cpu_power_data_zenpower(path);
            break;
        } else if (name == "zenergy") {
            cpuPowerData = (CPUPowerData*)init_cpu_power_data_zenergy(path);
            break;
        } else if (name == "coretemp") {
            intel = true;
        }
    }

    if (!cpuPowerData && intel) {
        std::string powercap = "/sys/class/powercap/";
        auto powercap_dirs = ls(powercap.c_str());
        for (auto& dir : powercap_dirs) {
            path = powercap + dir;
            name = read_line(path + "/name");
            SPDLOG_DEBUG("powercap: name: {}", name);
            if (name == "package-0") {
                cpuPowerData = (CPUPowerData*)init_cpu_power_data_rapl(path);
                break;
            }
        }
    }
    if (!cpuPowerData && !intel) {
        auto powerData = std::make_unique<CPUPowerData_amdgpu>();
        cpuPowerData = (CPUPowerData*)powerData.release();
    }

    if(cpuPowerData == nullptr) {
        SPDLOG_ERROR("Failed to initialize CPU power data");
        return false;
    }

    m_cpuPowerData.reset(cpuPowerData);
    return true;
}

CPUStats cpuStats;
