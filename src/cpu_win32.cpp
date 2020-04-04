#include <windows.h>
#include <thread>
#include "cpu.h"
#define SystemProcessorPerformanceInformation 0x8
#define SystemBasicInformation    0x0
FILETIME last_userTime, last_kernelTime, last_idleTime;

uint64_t FromFileTime( const FILETIME& ft ) {
    ULARGE_INTEGER uli = { 0 };
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

bool CPUStats::UpdateCPUData()
{
    FILETIME idleTime;
    FILETIME kernelTime;
    FILETIME userTime;
    BOOL res = GetSystemTimes( &idleTime, &kernelTime, &userTime );
    int usr = FromFileTime(userTime) - FromFileTime(last_userTime);
    int ker = FromFileTime(kernelTime) - FromFileTime(last_kernelTime);
    int idl = FromFileTime(idleTime) - FromFileTime(last_idleTime);
    int sys = ker + usr;
    m_cpuDataTotal.percent = (sys - idl) *100 / sys;
    last_userTime = userTime;
    last_kernelTime = kernelTime;
    last_idleTime = idleTime;
    return true;
}
CPUStats::CPUStats()
{
}
CPUStats::~CPUStats()
{
}
CPUStats cpuStats;