#pragma once
#ifndef MANGOHUD_CPU_H
#define MANGOHUD_CPU_H

#include <vector>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#ifdef WIN32
#include <windows.h>
#endif
#include "timing.hpp"
#include "gpu.h"

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

   int cpu_id;
   float percent;
   int mhz;
   int temp;
   int cpu_mhz;
   float power;
} CPUData;

enum {
   CPU_POWER_K10TEMP,
   CPU_POWER_ZENPOWER,
   CPU_POWER_ZENERGY,
   CPU_POWER_RAPL,
   CPU_POWER_AMDGPU
};

struct CPUPowerData {
   virtual ~CPUPowerData() = default;
   int source;
};

struct CPUPowerData_k10temp : public CPUPowerData {
   CPUPowerData_k10temp() {
      this->source = CPU_POWER_K10TEMP;
   };

   ~CPUPowerData_k10temp() {
      if(this->coreVoltageFile)
         fclose(this->coreVoltageFile);
      if(this->coreCurrentFile)
         fclose(this->coreCurrentFile);
      if(this->socVoltageFile)
         fclose(this->socVoltageFile);
      if(this->socCurrentFile)
         fclose(this->socCurrentFile);
      if(this->corePowerFile)
         fclose(this->corePowerFile);
      if(this->socPowerFile)
         fclose(this->socPowerFile);
   };

   FILE* coreVoltageFile {nullptr};
   FILE* coreCurrentFile {nullptr};
   FILE* socVoltageFile {nullptr};
   FILE* socCurrentFile {nullptr};
   FILE* corePowerFile {nullptr};
   FILE* socPowerFile {nullptr};
};

struct CPUPowerData_zenpower : public CPUPowerData {
   CPUPowerData_zenpower() {
      this->source = CPU_POWER_ZENPOWER;
   };

   ~CPUPowerData_zenpower() {
      if(this->corePowerFile)
         fclose(this->corePowerFile);
      if(this->socPowerFile)
         fclose(this->socPowerFile);
   };

   FILE* corePowerFile {nullptr};
   FILE* socPowerFile {nullptr};
};

struct CPUPowerData_zenergy : public CPUPowerData {
   CPUPowerData_zenergy() {
      this->source = CPU_POWER_ZENERGY;
      this->lastCounterValue = 0;
      this->lastCounterValueTime = Clock::now();
   };

   ~CPUPowerData_zenergy() {
      if(this->energyCounterFile)
         fclose(this->energyCounterFile);
   };

   FILE* energyCounterFile {nullptr};
   uint64_t lastCounterValue;
   Clock::time_point lastCounterValueTime;
};

struct CPUPowerData_rapl : public CPUPowerData {
   CPUPowerData_rapl() {
      this->source = CPU_POWER_RAPL;
      this->lastCounterValue = 0;
      this->lastCounterValueTime = Clock::now();
   };

   ~CPUPowerData_rapl() {
      if(this->energyCounterFile)
         fclose(this->energyCounterFile);
   };

   FILE* energyCounterFile {nullptr};
   uint64_t lastCounterValue;
   Clock::time_point lastCounterValueTime;
};

struct CPUPowerData_amdgpu : public CPUPowerData {
   CPUPowerData_amdgpu() {
      this->source = CPU_POWER_AMDGPU;
   };
};

class CPUStats
{
public:
   CPUStats();
   ~CPUStats();
   bool Init();
   bool Reinit();
   bool Updated()
   {
      return m_updatedCPUs;
   }

   bool UpdateCPUData();
   bool UpdateCoreMhz();
   bool UpdateCpuTemp();
   bool UpdateCpuPower();
   bool ReadcpuTempFile(int& temp);
   bool GetCpuFile();
   bool InitCpuPowerData();
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
   FILE *m_cpuTempFile = nullptr;
   std::unique_ptr<CPUPowerData> m_cpuPowerData;
};

extern CPUStats cpuStats;
#ifdef WIN32
uint64_t FileTimeToInt64( const FILETIME& ft );
#endif
#endif //MANGOHUD_CPU_H
