#include <stdio.h>
#include <tchar.h>

///
///  Copyright (c) 2008 - 2016 Advanced Micro Devices, Inc.

///  THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
///  EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
///  WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.

/// \file Source.cpp


#include <windows.h>
#include "adl/adl_sdk.h"

#include <stdio.h>
#include "gpu.h"
// Comment out one of the two lines below to allow or supress diagnostic messages
// #define PRINTF
#define PRINTF printf
bool init_adl = false;

// Definitions of the used function pointers. Add more if you use other ADL APIs
typedef int(*ADL_MAIN_CONTROL_CREATE)(ADL_MAIN_MALLOC_CALLBACK, int);
typedef int(*ADL_MAIN_CONTROL_DESTROY)();
typedef int(*ADL_FLUSH_DRIVER_DATA)(int);
typedef int(*ADL2_ADAPTER_ACTIVE_GET) (ADL_CONTEXT_HANDLE, int, int*);

typedef int(*ADL_ADAPTER_NUMBEROFADAPTERS_GET) (int*);
typedef int(*ADL_ADAPTER_ADAPTERINFO_GET) (LPAdapterInfo, int);
typedef int(*ADL_ADAPTERX2_CAPS) (int, int*);
typedef int(*ADL2_OVERDRIVE_CAPS) (ADL_CONTEXT_HANDLE context, int iAdapterIndex, int * iSupported, int * iEnabled, int * iVersion);
typedef int(*ADL2_OVERDRIVEN_CAPABILITIESX2_GET)	(ADL_CONTEXT_HANDLE, int, ADLODNCapabilitiesX2*);
typedef int(*ADL2_OVERDRIVEN_PERFORMANCESTATUS_GET) (ADL_CONTEXT_HANDLE, int, ADLODNPerformanceStatus*);
typedef int(*ADL2_OVERDRIVEN_FANCONTROL_GET) (ADL_CONTEXT_HANDLE, int, ADLODNFanControl*);
typedef int(*ADL2_OVERDRIVEN_FANCONTROL_SET) (ADL_CONTEXT_HANDLE, int, ADLODNFanControl*);
typedef int(*ADL2_OVERDRIVEN_POWERLIMIT_GET) (ADL_CONTEXT_HANDLE, int, ADLODNPowerLimitSetting*);
typedef int(*ADL2_OVERDRIVEN_POWERLIMIT_SET) (ADL_CONTEXT_HANDLE, int, ADLODNPowerLimitSetting*);
typedef int(*ADL2_OVERDRIVEN_TEMPERATURE_GET) (ADL_CONTEXT_HANDLE, int, int, int*);
typedef int(*ADL2_OVERDRIVEN_SYSTEMCLOCKSX2_GET)	(ADL_CONTEXT_HANDLE, int, ADLODNPerformanceLevelsX2*);
typedef int(*ADL2_OVERDRIVEN_SYSTEMCLOCKSX2_SET)	(ADL_CONTEXT_HANDLE, int, ADLODNPerformanceLevelsX2*);
typedef int(*ADL2_OVERDRIVEN_MEMORYCLOCKSX2_GET)	(ADL_CONTEXT_HANDLE, int, ADLODNPerformanceLevelsX2*);
typedef int(*ADL2_OVERDRIVEN_MEMORYCLOCKSX2_SET)	(ADL_CONTEXT_HANDLE, int, ADLODNPerformanceLevelsX2*);

typedef int(*ADL2_OVERDRIVEN_MEMORYTIMINGLEVEL_GET) (ADL_CONTEXT_HANDLE context, int iAdapterIndex, int *lpSupport, int *lpCurrentValue, int *lpDefaultValue, int *lpNumberLevels, int **lppLevelList);
typedef int(*ADL2_OVERDRIVEN_MEMORYTIMINGLEVEL_SET) (ADL_CONTEXT_HANDLE context, int iAdapterIndex, int currentValue);
typedef int(*ADL2_OVERDRIVEN_ZERORPMFAN_GET) (ADL_CONTEXT_HANDLE context, int iAdapterIndex, int *lpSupport, int *lpCurrentValue, int *lpDefaultValue);
typedef int(*ADL2_OVERDRIVEN_ZERORPMFAN_SET) (ADL_CONTEXT_HANDLE context, int iAdapterIndex, int currentValue);

typedef int(*ADL2_OVERDRIVEN_SETTINGSEXT_GET) (ADL_CONTEXT_HANDLE context, int iAdapterIndex, int* lpOverdriveNExtCapabilities, int *lpNumberOfODNExtFeatures, ADLODNExtSingleInitSetting** lppInitSettingList, int** lppCurrentSettingList);
typedef int(*ADL2_OVERDRIVEN_SETTINGSEXT_SET) (ADL_CONTEXT_HANDLE context, int iAdapterIndex, int iNumberOfODNExtFeatures, int* itemValueValidList, int* lpItemValueList);

HINSTANCE hDLL;

ADL_MAIN_CONTROL_CREATE          ADL_Main_Control_Create = NULL;
ADL_MAIN_CONTROL_DESTROY         ADL_Main_Control_Destroy = NULL;
ADL_ADAPTER_NUMBEROFADAPTERS_GET ADL_Adapter_NumberOfAdapters_Get = NULL;
ADL_ADAPTER_ADAPTERINFO_GET      ADL_Adapter_AdapterInfo_Get = NULL;
ADL_ADAPTERX2_CAPS ADL_AdapterX2_Caps = NULL;
ADL2_ADAPTER_ACTIVE_GET				ADL2_Adapter_Active_Get = NULL;
ADL2_OVERDRIVEN_CAPABILITIESX2_GET ADL2_OverdriveN_CapabilitiesX2_Get = NULL;
ADL2_OVERDRIVEN_SYSTEMCLOCKSX2_GET ADL2_OverdriveN_SystemClocksX2_Get = NULL;
ADL2_OVERDRIVEN_SYSTEMCLOCKSX2_SET ADL2_OverdriveN_SystemClocksX2_Set = NULL;
ADL2_OVERDRIVEN_PERFORMANCESTATUS_GET ADL2_OverdriveN_PerformanceStatus_Get = NULL;
ADL2_OVERDRIVEN_FANCONTROL_GET ADL2_OverdriveN_FanControl_Get = NULL;
ADL2_OVERDRIVEN_FANCONTROL_SET ADL2_OverdriveN_FanControl_Set = NULL;
ADL2_OVERDRIVEN_POWERLIMIT_GET ADL2_OverdriveN_PowerLimit_Get = NULL;
ADL2_OVERDRIVEN_POWERLIMIT_SET ADL2_OverdriveN_PowerLimit_Set = NULL;
ADL2_OVERDRIVEN_MEMORYCLOCKSX2_GET ADL2_OverdriveN_MemoryClocksX2_Get = NULL;
ADL2_OVERDRIVEN_MEMORYCLOCKSX2_SET ADL2_OverdriveN_MemoryClocksX2_Set = NULL;
ADL2_OVERDRIVE_CAPS ADL2_Overdrive_Caps = NULL;
ADL2_OVERDRIVEN_TEMPERATURE_GET ADL2_OverdriveN_Temperature_Get = NULL;

ADL2_OVERDRIVEN_MEMORYTIMINGLEVEL_GET ADL2_OverdriveN_MemoryTimingLevel_Get = NULL;
ADL2_OVERDRIVEN_MEMORYTIMINGLEVEL_SET ADL2_OverdriveN_MemoryTimingLevel_Set = NULL;
ADL2_OVERDRIVEN_ZERORPMFAN_GET ADL2_OverdriveN_ZeroRPMFan_Get = NULL;
ADL2_OVERDRIVEN_ZERORPMFAN_SET ADL2_OverdriveN_ZeroRPMFan_Set = NULL;

ADL2_OVERDRIVEN_SETTINGSEXT_GET ADL2_OverdriveN_SettingsExt_Get = NULL;
ADL2_OVERDRIVEN_SETTINGSEXT_SET ADL2_OverdriveN_SettingsExt_Set = NULL;

// Memory allocation function
void* __stdcall ADL_Main_Memory_Alloc(int iSize)
{
    void* lpBuffer = malloc(iSize);
    return lpBuffer;
}

// Optional Memory de-allocation function
void __stdcall ADL_Main_Memory_Free(void** lpBuffer)
{
    if (NULL != *lpBuffer)
    {
        free(*lpBuffer);
        *lpBuffer = NULL;
    }
}

ADL_CONTEXT_HANDLE context = NULL;

LPAdapterInfo   lpAdapterInfo = NULL;
int  iNumberAdapters;


int initializeADL()
{

    // Load the ADL dll
    hDLL = LoadLibrary(TEXT("atiadlxx.dll"));
    if (hDLL == NULL)
    {
        // A 32 bit calling application on 64 bit OS will fail to LoadLibrary.
        // Try to load the 32 bit library (atiadlxy.dll) instead
        hDLL = LoadLibrary(TEXT("atiadlxy.dll"));
    }

    if (NULL == hDLL)
    {
        PRINTF("Failed to load ADL library\n");
        return FALSE;
    }
    ADL_Main_Control_Create = (ADL_MAIN_CONTROL_CREATE)GetProcAddress(hDLL, "ADL_Main_Control_Create");
    ADL_Main_Control_Destroy = (ADL_MAIN_CONTROL_DESTROY)GetProcAddress(hDLL, "ADL_Main_Control_Destroy");
    ADL_Adapter_NumberOfAdapters_Get = (ADL_ADAPTER_NUMBEROFADAPTERS_GET)GetProcAddress(hDLL, "ADL_Adapter_NumberOfAdapters_Get");
    ADL_Adapter_AdapterInfo_Get = (ADL_ADAPTER_ADAPTERINFO_GET)GetProcAddress(hDLL, "ADL_Adapter_AdapterInfo_Get");
    ADL_AdapterX2_Caps = (ADL_ADAPTERX2_CAPS)GetProcAddress(hDLL, "ADL_AdapterX2_Caps");
    ADL2_Adapter_Active_Get = (ADL2_ADAPTER_ACTIVE_GET)GetProcAddress(hDLL, "ADL2_Adapter_Active_Get");
    ADL2_OverdriveN_CapabilitiesX2_Get = (ADL2_OVERDRIVEN_CAPABILITIESX2_GET)GetProcAddress(hDLL, "ADL2_OverdriveN_CapabilitiesX2_Get");
    ADL2_OverdriveN_SystemClocksX2_Get = (ADL2_OVERDRIVEN_SYSTEMCLOCKSX2_GET)GetProcAddress(hDLL, "ADL2_OverdriveN_SystemClocksX2_Get");
    ADL2_OverdriveN_SystemClocksX2_Set = (ADL2_OVERDRIVEN_SYSTEMCLOCKSX2_SET)GetProcAddress(hDLL, "ADL2_OverdriveN_SystemClocksX2_Set");
    ADL2_OverdriveN_MemoryClocksX2_Get = (ADL2_OVERDRIVEN_MEMORYCLOCKSX2_GET)GetProcAddress(hDLL, "ADL2_OverdriveN_MemoryClocksX2_Get");
    ADL2_OverdriveN_MemoryClocksX2_Set = (ADL2_OVERDRIVEN_MEMORYCLOCKSX2_SET)GetProcAddress(hDLL, "ADL2_OverdriveN_MemoryClocksX2_Set");
    ADL2_OverdriveN_PerformanceStatus_Get = (ADL2_OVERDRIVEN_PERFORMANCESTATUS_GET)GetProcAddress(hDLL, "ADL2_OverdriveN_PerformanceStatus_Get");
    ADL2_OverdriveN_FanControl_Get = (ADL2_OVERDRIVEN_FANCONTROL_GET)GetProcAddress(hDLL, "ADL2_OverdriveN_FanControl_Get");
    ADL2_OverdriveN_FanControl_Set = (ADL2_OVERDRIVEN_FANCONTROL_SET)GetProcAddress(hDLL, "ADL2_OverdriveN_FanControl_Set");
    ADL2_OverdriveN_PowerLimit_Get = (ADL2_OVERDRIVEN_POWERLIMIT_GET)GetProcAddress(hDLL, "ADL2_OverdriveN_PowerLimit_Get");
    ADL2_OverdriveN_PowerLimit_Set = (ADL2_OVERDRIVEN_POWERLIMIT_SET)GetProcAddress(hDLL, "ADL2_OverdriveN_PowerLimit_Set");
    ADL2_OverdriveN_Temperature_Get = (ADL2_OVERDRIVEN_TEMPERATURE_GET)GetProcAddress(hDLL, "ADL2_OverdriveN_Temperature_Get");
    ADL2_Overdrive_Caps = (ADL2_OVERDRIVE_CAPS)GetProcAddress(hDLL, "ADL2_Overdrive_Caps");

    ADL2_OverdriveN_MemoryTimingLevel_Get = (ADL2_OVERDRIVEN_MEMORYTIMINGLEVEL_GET)GetProcAddress(hDLL, "ADL2_OverdriveN_MemoryTimingLevel_Get");
    ADL2_OverdriveN_MemoryTimingLevel_Set = (ADL2_OVERDRIVEN_MEMORYTIMINGLEVEL_SET)GetProcAddress(hDLL, "ADL2_OverdriveN_MemoryTimingLevel_Set");
    ADL2_OverdriveN_ZeroRPMFan_Get = (ADL2_OVERDRIVEN_ZERORPMFAN_GET)GetProcAddress(hDLL, "ADL2_OverdriveN_ZeroRPMFan_Get");
    ADL2_OverdriveN_ZeroRPMFan_Set = (ADL2_OVERDRIVEN_ZERORPMFAN_SET)GetProcAddress(hDLL, "ADL2_OverdriveN_ZeroRPMFan_Set");

    ADL2_OverdriveN_SettingsExt_Get = (ADL2_OVERDRIVEN_SETTINGSEXT_GET)GetProcAddress(hDLL, "ADL2_OverdriveN_SettingsExt_Get");
    ADL2_OverdriveN_SettingsExt_Set = (ADL2_OVERDRIVEN_SETTINGSEXT_SET)GetProcAddress(hDLL, "ADL2_OverdriveN_SettingsExt_Set");

    if (NULL == ADL_Main_Control_Create ||
        NULL == ADL_Main_Control_Destroy ||
        NULL == ADL_Adapter_NumberOfAdapters_Get ||
        NULL == ADL_Adapter_AdapterInfo_Get ||
        NULL == ADL_AdapterX2_Caps ||
        NULL == ADL2_Adapter_Active_Get ||
        NULL == ADL2_OverdriveN_CapabilitiesX2_Get ||
        NULL == ADL2_OverdriveN_SystemClocksX2_Get ||
        NULL == ADL2_OverdriveN_SystemClocksX2_Set ||
        NULL == ADL2_OverdriveN_MemoryClocksX2_Get ||
        NULL == ADL2_OverdriveN_MemoryClocksX2_Set ||
        NULL == ADL2_OverdriveN_PerformanceStatus_Get ||
        NULL == ADL2_OverdriveN_FanControl_Get ||
        NULL == ADL2_OverdriveN_FanControl_Set ||
        NULL == ADL2_Overdrive_Caps ||
        NULL == ADL2_OverdriveN_MemoryTimingLevel_Get ||
        NULL == ADL2_OverdriveN_MemoryTimingLevel_Set ||
        NULL == ADL2_OverdriveN_ZeroRPMFan_Get ||
        NULL == ADL2_OverdriveN_ZeroRPMFan_Set ||
        NULL == ADL2_OverdriveN_SettingsExt_Get ||
        NULL == ADL2_OverdriveN_SettingsExt_Set
        )
    {
        PRINTF("Failed to get ADL function pointers\n");
        return FALSE;
    }

    if (ADL_OK != ADL_Main_Control_Create(ADL_Main_Memory_Alloc, 1))
    {
        printf("Failed to initialize nested ADL2 context");
        return ADL_ERR;
    }
    if (ADL_OK != ADL_Adapter_NumberOfAdapters_Get(&iNumberAdapters))
    {
        PRINTF("Cannot get the number of adapters!\n");
        return 0;
    }
    if (0 < iNumberAdapters)
    {
        lpAdapterInfo = (LPAdapterInfo)malloc(sizeof(AdapterInfo)* iNumberAdapters);
        memset(lpAdapterInfo, '\0', sizeof(AdapterInfo)* iNumberAdapters);

        // Get the AdapterInfo structure for all adapters in the system
        ADL_Adapter_AdapterInfo_Get(lpAdapterInfo, sizeof(AdapterInfo)* iNumberAdapters);
    }
    printf("num adapters: %i", iNumberAdapters);
    return TRUE;
}

void deinitializeADL()
{

    ADL_Main_Control_Destroy();

    FreeLibrary(hDLL);


}

int query_adl()
{   
    if (!init_adl)
        init_adl = initializeADL();
        
        ADLODNPerformanceStatus odNPerformanceStatus;
        memset(&odNPerformanceStatus, 0, sizeof(ADLODNPerformanceStatus));

        if (ADL_OK != ADL2_OverdriveN_PerformanceStatus_Get(context, lpAdapterInfo[0].iAdapterIndex, &odNPerformanceStatus))
            printf("MANGOHUD: Adl can't get GPU load");
        else 
            gpu_info.load = odNPerformanceStatus.iGPUActivityPercent;

    return 0;
}

uint32_t adl_vendorid(){
    return lpAdapterInfo[0].iVendorID;
}