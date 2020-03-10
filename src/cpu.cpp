#include "cpu.h"
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
FILE *cpuTempFile = nullptr;
pthread_t cpuTempThread;

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
	m_inited = Init();
}

bool CPUStats::Init()
{
	CPUStats::GetCpuFile();
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

	UpdateCPUData();
	return true;
}

//TODO take sampling interval into account?
bool CPUStats::UpdateCPUData()
{
    CPUStats::UpdateCoreMhz();
	CPUStats::UpdateCpuTemp();
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
    return true;
}

bool CPUStats::UpdateCpuTemp(){
    rewind(cpuTempFile);
    fflush(cpuTempFile);
    if (fscanf(cpuTempFile, "%d", &m_cpuDataTotal.temp) != 1)
        m_cpuDataTotal.temp = 0;
    m_cpuDataTotal.temp /= 1000;

    return NULL;
}

bool CPUStats::GetCpuFile(){
	std::string name, path;
	std::string hwmon = "/sys/class/hwmon/";
	auto dirs = ls(hwmon.c_str());
	for (auto& dir : dirs)
	{
	path = hwmon + dir;
	name = read_line(path + "/name");
#ifndef NDEBUG
		std::cerr << "hwmon: sensor name: " << name << std::endl;
#endif
		if (name == "coretemp" || name == "k10temp" || name == "zenpower"){
		path += "/temp1_input";
		break;
		}
	}
	if (!file_exists(path)) {
		std::cerr << "MANGOHUD: Could not find cpu temp sensor location" << std::endl;
	} else {
		cpuTempFile = fopen(path.c_str(), "r");
	}
	return true;
}

CPUStats cpuStats;