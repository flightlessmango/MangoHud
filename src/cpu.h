#pragma once
#ifndef MANGOHUD_CPU_H
#define MANGOHUD_CPU_H

#include <vector>
#include <cstdint>
#include <cstdio>

typedef struct CPUData_ {
   uint64_t totalTime;
   uint64_t userTime;
   uint64_t systemTime;
   uint64_t systemAllTime;
   uint64_t idleAllTime;
   uint64_t idleTime;
   uint64_t niceTime;
   uint64_t ioWaitTime;
   uint64_t irqTime;
   uint64_t softIrqTime;
   uint64_t stealTime;
   uint64_t guestTime;

   uint64_t totalPeriod;
   uint64_t userPeriod;
   uint64_t systemPeriod;
   uint64_t systemAllPeriod;
   uint64_t idleAllPeriod;
   uint64_t idlePeriod;
   uint64_t nicePeriod;
   uint64_t ioWaitPeriod;
   uint64_t irqPeriod;
   uint64_t softIrqPeriod;
   uint64_t stealPeriod;
   uint64_t guestPeriod;
   float percent;
   int mhz;
   int temp;
} CPUData;

class CPUStats
{
public:
   CPUStats();
   ~CPUStats();
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
   uint64_t m_boottime = 0;
   std::vector<CPUData> m_cpuData;
   CPUData m_cpuDataTotal {};
   std::vector<int> m_coreMhz;
   double m_cpuPeriod = 0;
   bool m_updatedCPUs = false; // TODO use caching or just update?
   bool m_inited = false;
   FILE *m_cpuTempFile = nullptr;
};

extern CPUStats cpuStats;

#endif //MANGOHUD_CPU_H
