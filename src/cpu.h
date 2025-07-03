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

   std::string label = "unknown";
} CPUData;

enum {
   CPU_POWER_K10TEMP,
   CPU_POWER_ZENPOWER,
   CPU_POWER_ZENERGY,
   CPU_POWER_RAPL,
   CPU_POWER_AMDGPU,
   CPU_POWER_XGENE
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


struct CPUPowerData_xgene : public CPUPowerData {
   CPUPowerData_xgene() {
      this->source = CPU_POWER_XGENE;
   };

   ~CPUPowerData_xgene() {
      if(this->powerFile)
         fclose(this->powerFile);
   };

   FILE* powerFile {nullptr};
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
   void get_cpu_cores_types();
   void get_cpu_cores_types_intel();
   void get_cpu_cores_types_arm();

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

   const std::map<std::string, std::string> intel_cores = {
      {"P", "/sys/devices/cpu_core/cpus"},
      {"E", "/sys/devices/cpu_atom/cpus"}
   };

   const std::map<std::string, std::string> arm_cores = {
      // Performance cores
      {"0xd07", "A57"},
      {"0xd08", "A72"},
      {"0xd09", "A73"},
      {"0xd0a", "A75"},
      {"0xd0b", "A76"},
      {"0xd0c", "A77"},
      {"0xd41", "A78"},
      {"0xd44", "X1"},
      {"0xd4d", "X2"},
      {"0xd4e", "X3"},
      {"0xd47", "A710"},
      {"0xd4f", "A720"},
      {"0xd4b", "X4"},

      // Efficiency Cores
      {"0xd03", "A53"},
      {"0xd05", "A55"},
      {"0xd46", "A510"},
      {"0xd4a", "A520"},

      // General-Purpose Cores
      {"0xd04", "A35"},
      {"0xd06", "A65"}
   };
};

extern CPUStats cpuStats;
#ifdef WIN32
uint64_t FileTimeToInt64( const FILETIME& ft );
#endif
#endif //MANGOHUD_CPU_H
