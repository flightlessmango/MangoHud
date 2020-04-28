#include <windows.h>
#include <thread>
#include <string.h>
#include "cpu.h"
#include <iostream>
#define SystemProcessorPerformanceInformation 0x8
#define SystemBasicInformation    0x0
FILETIME last_userTime, last_kernelTime, last_idleTime;

uint64_t FileTimeToInt64( const FILETIME& ft ) {
    ULARGE_INTEGER uli = { 0 };
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

bool CPUStats::UpdateCPUData()
{
    #define NUMBER_OF_PROCESSORS (8)
    #define PROCESSOR_BUFFER_SIZE (NUMBER_OF_PROCESSORS * 8)
    static ULONG64 ProcessorIdleTimeBuffer [ PROCESSOR_BUFFER_SIZE ];

    FILETIME IdleTime, KernelTime, UserTime;
    static unsigned long long PrevTotal = 0;
    static unsigned long long PrevIdle = 0;
    static unsigned long long PrevUser = 0;
    unsigned long long ThisTotal;
    unsigned long long ThisIdle, ThisKernel, ThisUser;
    unsigned long long TotalSinceLast, IdleSinceLast, UserSinceLast;


    // GET THE KERNEL / USER / IDLE times.  
    // And oh, BTW, kernel time includes idle time
    GetSystemTimes( & IdleTime, & KernelTime, & UserTime);

    ThisIdle = FileTimeToInt64(IdleTime);
    ThisKernel = FileTimeToInt64 (KernelTime);
    ThisUser = FileTimeToInt64 (UserTime);

    ThisTotal = ThisKernel + ThisUser;
    TotalSinceLast = ThisTotal - PrevTotal;
    IdleSinceLast = ThisIdle - PrevIdle;
    UserSinceLast = ThisUser - PrevUser;
    double Headroom;
    Headroom =  (double)IdleSinceLast / (double)TotalSinceLast ;
    double Load;
    Load = 1.0 - Headroom;
    Headroom *= 100.0;  // to make it percent
    Load *= 100.0;  // percent

    PrevTotal = ThisTotal;
    PrevIdle = ThisIdle;
    PrevUser = ThisUser;

    // print results to output window of VS when run in Debug
    m_cpuDataTotal.percent = Load;
    return true;
}
CPUStats::CPUStats()
{
}
CPUStats::~CPUStats()
{
}
CPUStats cpuStats;