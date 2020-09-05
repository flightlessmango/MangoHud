#include <dxgi.h>
#include <dxgi1_5.h>
#include <dxgi1_4.h>
#ifdef _MSC_VER
    #include <d3d12.h>
#else
    #include "/usr/include/wine/windows/d3d12.h"
#endif
#ifndef __D3D12_IMPL_H__
#define __D3D12_IMPL_H__

namespace impl
{
	namespace d3d12
	{
		void init();
		void uninit();
	}
}


#endif // __D3D12_IMPL_H__