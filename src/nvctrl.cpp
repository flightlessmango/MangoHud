#include "nvctrl.h"
Display *display = XOpenDisplay(NULL);

int nvidiaMemUsed(){
		int ret;
		XNVCTRLQueryTargetAttribute(display,
					NV_CTRL_TARGET_TYPE_GPU,
					0,
					0,
					NV_CTRL_USED_DEDICATED_GPU_MEMORY,
					&ret);
		return ret;
}