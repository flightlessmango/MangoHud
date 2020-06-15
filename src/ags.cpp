//
// Copyright (c) 2019 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

//-----------------------------------------------------------------------------
// File: AGSSample.cpp
//-----------------------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <crtdbg.h>
#include <amd_ags.h>



const char* getVendorName( int vendorId )
{
    switch ( vendorId )
    {
        case 0x1002: return "AMD";
        case 0x8086: return "INTEL";
        case 0x10DE: return "NVIDIA";
        default: return "unknown";
    }
}


void PrintDisplayInfo( const AGSGPUInfo& gpuInfo )
{
    for ( int gpuIndex = 0; gpuIndex < gpuInfo.numDevices; gpuIndex++ )
    {
        const AGSDeviceInfo& device = gpuInfo.devices[ gpuIndex ];

        printf( "\n---------- Device %d%s, %s\n", gpuIndex, device.isPrimaryDevice ? " [primary]" : "", device.adapterString );

        printf( "Vendor id:   0x%04X (%s)\n", device.vendorId, getVendorName( device.vendorId ) );
        printf( "Device id:   0x%04X\n", device.deviceId );
        printf( "Revision id: 0x%04X\n\n", device.revisionId );

        const char* asicFamily[] =
        {
            "unknown",
            "Pre GCN",
            "GCN Gen1",
            "GCN Gen2",
            "GCN Gen3",
            "GCN Gen4",
            "Vega",
            "RDNA"
        };

        if ( device.vendorId == 0x1002 )
        {
            char wgpInfo[ 256 ] = {};
            if ( device.asicFamily >= AGSDeviceInfo::AsicFamily_RDNA )
            {
                sprintf_s( wgpInfo, ", %d WGPs", device.numWGPs );
            }

            printf( "Architecture: %s, %d CUs%s, %d ROPs\n", asicFamily[ device.asicFamily ], device.numCUs, wgpInfo, device.numROPs );
            printf( "    core clock %d MHz, memory clock %d MHz\n", device.coreClock, device.memoryClock );
            printf( "    %.1f Tflops\n", device.teraFlops );
            printf( "local memory: %d MBs (%.1f GB/s)\n\n", (int)( device.localMemoryInBytes / ( 1024 * 1024 ) ), (float)device.memoryBandwidth / 1024.0f );
        }

        printf( "\n" );

        if ( device.eyefinityEnabled )
        {
            printf( "SLS grid is %d displays wide by %d displays tall\n", device.eyefinityGridWidth, device.eyefinityGridHeight );
            printf( "SLS resolution is %d x %d pixels%s\n", device.eyefinityResolutionX, device.eyefinityResolutionY, device.eyefinityBezelCompensated ? ", bezel-compensated" : "" );
        }
        else
        {
            printf( "Eyefinity not enabled on this device\n" );
        }

        printf( "\n" );

        for ( int i = 0; i < device.numDisplays; i++ )
        {
            const AGSDisplayInfo& display = device.displays[ i ];

            printf( "\t---------- Display %d %s----------------------------------------\n", i, display.displayFlags & AGS_DISPLAYFLAG_PRIMARY_DISPLAY ? "[primary]" : "---------" );

            printf( "\tdevice name: %s\n", display.displayDeviceName );
            printf( "\tmonitor name: %s\n\n", display.name );

            printf( "\tMax resolution:             %d x %d, %.1f Hz\n", display.maxResolutionX, display.maxResolutionY, display.maxRefreshRate );
            printf( "\tCurrent resolution:         %d x %d, Offset (%d, %d), %.1f Hz\n", display.currentResolution.width, display.currentResolution.height, display.currentResolution.offsetX, display.currentResolution.offsetY, display.currentRefreshRate );
            printf( "\tVisible resolution:         %d x %d, Offset (%d, %d)\n\n", display.visibleResolution.width, display.visibleResolution.height, display.visibleResolution.offsetX, display.visibleResolution.offsetY );

            printf( "\tchromaticity red:           %f, %f\n", display.chromaticityRedX, display.chromaticityRedY );
            printf( "\tchromaticity green:         %f, %f\n", display.chromaticityGreenX, display.chromaticityGreenY );
            printf( "\tchromaticity blue:          %f, %f\n", display.chromaticityBlueX, display.chromaticityBlueY );
            printf( "\tchromaticity white point:   %f, %f\n\n", display.chromaticityWhitePointX, display.chromaticityWhitePointY );

            printf( "\tluminance: [min, max, avg]  %f, %f, %f\n", display.minLuminance, display.maxLuminance, display.avgLuminance );

            printf( "\tscreen reflectance diffuse  %f\n", display.screenDiffuseReflectance );
            printf( "\tscreen reflectance specular %f\n\n", display.screenSpecularReflectance );

            if ( display.displayFlags & AGS_DISPLAYFLAG_HDR10 )
                printf( "\tHDR10 supported\n" );

            if ( display.displayFlags & AGS_DISPLAYFLAG_DOLBYVISION )
                printf( "\tDolby Vision supported\n" );

            if ( display.displayFlags & AGS_DISPLAYFLAG_FREESYNC )
                printf( "\tFreesync supported\n" );

            if ( display.displayFlags & AGS_DISPLAYFLAG_FREESYNC_HDR )
                printf( "\tFreesync HDR supported\n" );

            printf( "\n" );

            if ( display.displayFlags & AGS_DISPLAYFLAG_EYEFINITY_IN_GROUP )
            {
                printf( "\tEyefinity Display [%s mode] %s\n", display.displayFlags & AGS_DISPLAYFLAG_EYEFINITY_IN_PORTRAIT_MODE ? "portrait" : "landscape", display.displayFlags & AGS_DISPLAYFLAG_EYEFINITY_PREFERRED_DISPLAY ? " (preferred display)" : "" );

                printf( "\tGrid coord [%d, %d]\n", display.eyefinityGridCoordX, display.eyefinityGridCoordY );
            }

            printf( "\tlogical display index: %d\n", display.logicalDisplayIndex );
            printf( "\tADL adapter index: %d\n\n", display.adlAdapterIndex );

            printf( "\n" );
        }
    }
}


void testDriver( const char* driver, unsigned int driverToCompareAgainst )
{
    AGSDriverVersionResult result = agsCheckDriverVersion( driver, driverToCompareAgainst );

    // int major = (driverToCompareAgainst & 0xFFC00000) >> 22;
    // int minor = (driverToCompareAgainst & 0x003FF000) >> 12;
    // int patch = (driverToCompareAgainst & 0x00000FFF);

    // if ( result == AGS_SOFTWAREVERSIONCHECK_UNDEFINED )
    // {
    //     printf( "Driver check could not determine the driver version for %s\n", driver );
    // }
    // else
    // {
    //     printf( "Driver check shows the installed %s driver is %s the %d.%d.%d required version\n", driver, result == AGS_SOFTWAREVERSIONCHECK_OK ? "newer or the same as" : "older than", major, minor, patch );
    // }
}

int main(int , char**)
{
    // Enable run-time memory check for debug builds.
    // (When _DEBUG is not defined, calls to _CrtSetDbgFlag are removed during preprocessing.)
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );

    AGSContext* agsContext = nullptr;

    int displayIndex = 0;
    DISPLAY_DEVICEA displayDevice;
    displayDevice.cb = sizeof( displayDevice );
    while ( EnumDisplayDevicesA( 0, displayIndex, &displayDevice, 0 ) )
    {
        printf( "Display Device: %d: %s, %s %s%s\n", displayIndex, displayDevice.DeviceString, displayDevice.DeviceName, displayDevice.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE ? "(primary)" : "", displayDevice.StateFlags & DISPLAY_DEVICE_ACTIVE ? "" : " [disabled]" );
        displayIndex++;
    }

    // {
    //     printf( "\n" );
    //     testDriver( "18.8.randombetadriver", AGS_MAKE_VERSION( 18, 8, 2 ) );
    //     testDriver( "18.8.123randomdriver", AGS_MAKE_VERSION( 18, 8, 2 ) );
    //     testDriver( "18.9.randomdriver", AGS_MAKE_VERSION( 18, 8, 2 ) );
    //     testDriver( "18.8.2", AGS_MAKE_VERSION( 18, 8, 2 ) );
    //     testDriver( "18.8.2", AGS_MAKE_VERSION( 18, 8, 1 ) );
    //     testDriver( "18.8.2", AGS_MAKE_VERSION( 18, 8, 3 ) );
    //     printf( "\n" );
    // }

    AGSGPUInfo gpuInfo;

    AGSConfiguration config = {};
    if ( agsInit( &agsContext, &config, &gpuInfo ) == AGS_SUCCESS )
    {
        printf( "\nAGS Library initialized: v%d.%d.%d\n", gpuInfo.agsVersionMajor, gpuInfo.agsVersionMinor, gpuInfo.agsVersionPatch );
        printf( "Is%s WACK compliant for use in UWP apps\n", gpuInfo.isWACKCompliant ? "" : " *not*" );
        printf( "-----------------------------------------------------------------\n" );

        printf( "Radeon Software Version:   %s\n", gpuInfo.radeonSoftwareVersion );
        printf( "Driver Version:            %s\n", gpuInfo.driverVersion );
        printf( "-----------------------------------------------------------------\n" );
        PrintDisplayInfo( gpuInfo );
        printf( "-----------------------------------------------------------------\n" );

        if ( agsDeInit( agsContext ) != AGS_SUCCESS )
        {
            printf( "Failed to cleanup AGS Library\n" );
        }
    }
    else
    {
        printf( "Failed to initialize AGS Library\n" );
    }

    printf( "\ndone\n" );

    _getch();

    return 0;
}

