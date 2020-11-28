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
#include "string_utils.h"

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

void calculateCPUData(CPUData& cpuData,
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
        std::cerr << "Failed to opening " << PROCSTATFILE << std::endl;
        return false;
    }

    do {
        if (!std::getline(file, line)) {
            std::cerr << "Failed to read all of " << PROCSTATFILE << std::endl;
            return false;
        } else if (starts_with(line, "cpu")) {
            if (first) {
                first =false;
                continue;
            }

            CPUData cpu = {};
            cpu.totalTime = 1;
            cpu.totalPeriod = 1;
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

//TODO take sampling interval into account?
bool CPUStats::UpdateCPUData()
{
    unsigned long long int usertime, nicetime, systemtime, idletime;
    unsigned long long int ioWait, irq, softIrq, steal, guest, guestnice;
    int cpuid = -1;

    if (!m_inited)
        return false;

    std::string line;
    std::ifstream file (PROCSTATFILE);
    bool ret = false;

    if (!file.is_open()) {
        std::cerr << "Failed to opening " << PROCSTATFILE << std::endl;
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

            //std::cerr << "Parsing 'cpu" << cpuid << "' line:" <<  line << std::endl;

            if (!ret) {
                //std::cerr << "Failed to parse 'cpu' line" << std::endl;
                std::cerr << "Failed to parse 'cpu' line:" <<  line << std::endl;
                return false;
            }

            if (cpuid < 0 /* can it? */ || (size_t)cpuid > m_cpuData.size()) {
                std::cerr << "Cpu id '" << cpuid << "' is out of bounds" << std::endl;
                return false;
            }

            CPUData& cpuData = m_cpuData[cpuid];
            calculateCPUData(cpuData, usertime, nicetime, systemtime, idletime, ioWait, irq, softIrq, steal, guest, guestnice);
            cpuid = -1;

        } else {
            break;
        }
    } while(true);

    m_cpuPeriod = (double)m_cpuData[0].totalPeriod / m_cpuData.size();
    m_updatedCPUs = true;
    return ret;
}

bool CPUStats::UpdateCoreMhz() {
    m_coreMhz.clear();
    std::ifstream cpuInfo(PROCCPUINFOFILE);
    std::string row;
    size_t i = 0;
    while (std::getline(cpuInfo, row) && i < m_cpuData.size()) {
        if (row.find("MHz") != std::string::npos){
            row = std::regex_replace(row, std::regex(R"([^0-9.])"), "");
            if (!try_stoi(m_cpuData[i].mhz, row))
                m_cpuData[i].mhz = 0;
            i++;
        }
    }
    m_cpuDataTotal.cpu_mhz = 0;
    for (auto data : m_cpuData)
        m_cpuDataTotal.cpu_mhz += data.mhz;
    m_cpuDataTotal.cpu_mhz /= m_cpuData.size();
    return true;
}

bool CPUStats::UpdateCpuTemp() {
    if (!m_cpuTempFile)
        return false;

    int temp = 0;
    rewind(m_cpuTempFile);
    fflush(m_cpuTempFile);
    bool ret = (fscanf(m_cpuTempFile, "%d", &temp) == 1);
    m_cpuDataTotal.temp = temp / 1000;

    return ret;
}

static bool get_cpu_power_k10temp(CPUPowerData* cpuPowerData, int& power) {
    CPUPowerData_k10temp* powerData_k10temp = (CPUPowerData_k10temp*)cpuPowerData;

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

static bool get_cpu_power_zenpower(CPUPowerData* cpuPowerData, int& power) {
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

static bool get_cpu_power_rapl(CPUPowerData* cpuPowerData, int& power) {
    CPUPowerData_rapl* powerData_rapl = (CPUPowerData_rapl*)cpuPowerData;

    if (!powerData_rapl->energyCounterFile)
        return false;

    rewind(powerData_rapl->energyCounterFile);
    fflush(powerData_rapl->energyCounterFile);

    int energyCounterValue = 0;
    if (fscanf(powerData_rapl->energyCounterFile, "%d", &energyCounterValue) != 1)
        return false;

    Clock::time_point now = Clock::now();
    Clock::duration timeDiff = now - powerData_rapl->lastCounterValueTime;
    int energyCounterDiff = energyCounterValue - powerData_rapl->lastCounterValue;

    power = (int)((float)energyCounterDiff / (float)timeDiff.count() * 1000);

    powerData_rapl->lastCounterValue = energyCounterValue;
    powerData_rapl->lastCounterValueTime = now;

    return true;
}

bool CPUStats::UpdateCpuPower() {
    if(!m_cpuPowerData)
        return false;

    int power = 0;

    switch(m_cpuPowerData->source) {
        case CPU_POWER_K10TEMP:
            if (!get_cpu_power_k10temp(m_cpuPowerData.get(), power)) return false;
            break;
        case CPU_POWER_ZENPOWER:
            if (!get_cpu_power_zenpower(m_cpuPowerData.get(), power)) return false;
            break;
        case CPU_POWER_RAPL:
            if (!get_cpu_power_rapl(m_cpuPowerData.get(), power)) return false;
            break;
        default:
            return false;
    }

    m_cpuDataTotal.power = power;

    return true;
}

static bool find_temp_input(const std::string path, std::string& input, const std::string& name)
{
    auto files = ls(path.c_str(), "temp", LS_FILES);
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

static bool find_fallback_temp_input(const std::string path, std::string& input)
{
    auto files = ls(path.c_str(), "temp", LS_FILES);
    if (!files.size())
        return false;

    std::sort(files.begin(), files.end());
    for (auto& file : files) {
        if (!ends_with(file, "_input"))
            continue;
        input = path + "/" + file;
#ifndef NDEBUG
        std::cerr << "fallback cpu temp input: " << input << "\n";
#endif
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
#ifndef NDEBUG
        std::cerr << "hwmon: sensor name: " << name << std::endl;
#endif
        if (name == "coretemp") {
            find_temp_input(path, input, "Package id 0");
            break;
        }
        else if ((name == "zenpower" || name == "k10temp")) {
            find_temp_input(path, input, "Tdie");
            break;
        } else if (name == "atk0110") {
            find_temp_input(path, input, "CPU Temperature");
            break;
        } else {
            path.clear();
        }
    }

    if (path.empty() || (!file_exists(input) && !find_fallback_temp_input(path, input))) {
        std::cerr << "MANGOHUD: Could not find cpu temp sensor location" << std::endl;
        return false;
    } else {
#ifndef NDEBUG
        std::cerr << "hwmon: using input: " << input << std::endl;
#endif
        m_cpuTempFile = fopen(input.c_str(), "r");
    }
    return true;
}

static bool find_voltage_input(const std::string path, std::string& input, const std::string& name)
{
    auto files = ls(path.c_str(), "in", LS_FILES);
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

static bool find_current_input(const std::string path, std::string& input, const std::string& name)
{
    auto files = ls(path.c_str(), "curr", LS_FILES);
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

static bool find_power_input(const std::string path, std::string& input, const std::string& name)
{
    auto files = ls(path.c_str(), "power", LS_FILES);
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

CPUPowerData_k10temp* init_cpu_power_data_k10temp(const std::string path) {
    auto powerData = std::make_unique<CPUPowerData_k10temp>();

    std::string coreVoltageInput, coreCurrentInput;
    std::string socVoltageInput, socCurrentInput;

    if(!find_voltage_input(path, coreVoltageInput, "Vcore")) return nullptr;
    if(!find_current_input(path, coreCurrentInput, "Icore")) return nullptr;
    if(!find_voltage_input(path, socVoltageInput, "Vsoc")) return nullptr;
    if(!find_current_input(path, socCurrentInput, "Isoc")) return nullptr;

#ifndef NDEBUG
    std::cerr << "hwmon: using input: " << coreVoltageInput << std::endl;
    std::cerr << "hwmon: using input: " << coreCurrentInput << std::endl;
    std::cerr << "hwmon: using input: " << socVoltageInput << std::endl;
    std::cerr << "hwmon: using input: " << socCurrentInput << std::endl;
#endif

    powerData->coreVoltageFile = fopen(coreVoltageInput.c_str(), "r");
    powerData->coreCurrentFile = fopen(coreCurrentInput.c_str(), "r");
    powerData->socVoltageFile = fopen(socVoltageInput.c_str(), "r");
    powerData->socCurrentFile = fopen(socCurrentInput.c_str(), "r");

    return powerData.release();
}

CPUPowerData_zenpower* init_cpu_power_data_zenpower(const std::string path) {
    auto powerData = std::make_unique<CPUPowerData_zenpower>();

    std::string corePowerInput, socPowerInput;

    if(!find_power_input(path, corePowerInput, "SVI2_P_Core")) return nullptr;
    if(!find_power_input(path, socPowerInput, "SVI2_P_SoC")) return nullptr;

#ifndef NDEBUG
    std::cerr << "hwmon: using input: " << corePowerInput << std::endl;
    std::cerr << "hwmon: using input: " << socPowerInput << std::endl;
#endif

    powerData->corePowerFile = fopen(corePowerInput.c_str(), "r");
    powerData->socPowerFile = fopen(socPowerInput.c_str(), "r");

    return powerData.release();
}

CPUPowerData_rapl* init_cpu_power_data_rapl(const std::string path) {
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

    CPUPowerData* cpuPowerData = nullptr;

    auto dirs = ls(hwmon.c_str());
    for (auto& dir : dirs) {
        path = hwmon + dir;
        name = read_line(path + "/name");
#ifndef NDEBUG
        std::cerr << "hwmon: sensor name: " << name << std::endl;
#endif
        if (name == "k10temp") {
            cpuPowerData = (CPUPowerData*)init_cpu_power_data_k10temp(path);
            break;
        } else if (name == "zenpower") {
            cpuPowerData = (CPUPowerData*)init_cpu_power_data_zenpower(path);
            break;
        }
    }

    if (!cpuPowerData) {
        std::string powercap = "/sys/class/powercap/";
        auto powercap_dirs = ls(powercap.c_str());
        for (auto& dir : powercap_dirs) {
            path = powercap + dir;
            name = read_line(path + "/name");
#ifndef NDEBUG
            std::cerr << "powercap: name: " << name << std::endl;
#endif
            if (name == "package-0") {
                cpuPowerData = (CPUPowerData*)init_cpu_power_data_rapl(path);
                break;
            }
        }
    }

    if(cpuPowerData == nullptr) {
        std::cerr << "MANGOHUD: Failed to initialize CPU power data" << std::endl;
        return false;
    }

    m_cpuPowerData.reset(cpuPowerData);
    return true;
}

CPUStats cpuStats;
