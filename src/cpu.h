#include <vector>
#include <cstdint>

typedef struct CPUData_ {
	unsigned long long int totalTime;
	unsigned long long int userTime;
	unsigned long long int systemTime;
	unsigned long long int systemAllTime;
	unsigned long long int idleAllTime;
	unsigned long long int idleTime;
	unsigned long long int niceTime;
	unsigned long long int ioWaitTime;
	unsigned long long int irqTime;
	unsigned long long int softIrqTime;
	unsigned long long int stealTime;
	unsigned long long int guestTime;

	unsigned long long int totalPeriod;
	unsigned long long int userPeriod;
	unsigned long long int systemPeriod;
	unsigned long long int systemAllPeriod;
	unsigned long long int idleAllPeriod;
	unsigned long long int idlePeriod;
	unsigned long long int nicePeriod;
	unsigned long long int ioWaitPeriod;
	unsigned long long int irqPeriod;
	unsigned long long int softIrqPeriod;
	unsigned long long int stealPeriod;
	unsigned long long int guestPeriod;
    float percent;
    int mhz;
	int temp;
} CPUData;

class CPUStats
{
public:
	CPUStats();
	bool Init();
	bool Updated()
	{
		return m_updatedCPUs;
	}

	bool UpdateCPUData();
    bool UpdateCoreMhz();
	bool UpdateCpuTemp();
	bool GetCpuFile();
	double GetCPUPeriod() { return m_cpuPeriod; }

	const std::vector<CPUData>& GetCPUData() const {
		return m_cpuData;
	}
	const CPUData& GetCPUDataTotal() const {
		return m_cpuDataTotal;
	}
private:
	unsigned long long int m_boottime = 0;
	std::vector<CPUData> m_cpuData;
	CPUData m_cpuDataTotal {};
	std::vector<int> m_coreMhz;
	double m_cpuPeriod = 0;
	bool m_updatedCPUs = false; // TODO use caching or just update?
	bool m_inited = false;
};

extern CPUStats cpuStats;