#include <iostream>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <memory>
#include <functional>
#include "nvctrl.h"
#include "loaders/loader_nvctrl.h"
#include "loaders/loader_x11.h"
#include "string_utils.h"

typedef std::unordered_map<std::string, std::string> string_map;
static std::unique_ptr<Display, std::function<void(Display*)>> display;

libnvctrl_loader nvctrl("libXNVCtrl.so.0");

struct nvctrlInfo nvctrl_info;
bool nvctrlSuccess = false;

static bool find_nv_x11(Display*& dpy)
{
    char buf[8] {};
    for (int i = 0; i < 16; i++) {
        snprintf(buf, sizeof(buf), ":%d", i);
        Display *d = g_x11->XOpenDisplay(buf);
        if (d) {
            if (nvctrl.XNVCTRLIsNvScreen(d, 0)) {
                dpy = d;
                std::cerr << "MANGOHUD: XNVCtrl is using display " << buf << std::endl;
                return true;
            }
            g_x11->XCloseDisplay(d);
        }
    }
    return false;
}

bool checkXNVCtrl()
{
    if (!g_x11->IsLoaded()) {
        std::cerr << "MANGOHUD: XNVCtrl: X11 loader failed to load\n";
        return false;
    }

    if (!nvctrl.IsLoaded()) {
        std::cerr << "MANGOHUD: XNVCtrl loader failed to load\n";
        return false;
    }

    Display *dpy;
    nvctrlSuccess = find_nv_x11(dpy);

    if (!nvctrlSuccess) {
        std::cerr << "MANGOHUD: XNVCtrl didn't find the correct display" << std::endl;
        return false;
    }

    auto local_x11 = g_x11;
    display = { dpy,
        [local_x11](Display *dpy) {
            local_x11->XCloseDisplay(dpy);
        }
    };

    return true;
}

static void parse_token(std::string token, string_map& options) {
    std::string param, value;

    size_t equal = token.find("=");
    if (equal == std::string::npos)
        return;

    value = token.substr(equal+1);

    param = token.substr(0, equal);
    trim(param);
    trim(value);
    //std::cerr << __func__ << ": " << param << "=" << value << std::endl;
    if (!param.empty())
        options[param] = value;
}

char* get_attr_target_string(int attr, int target_type, int target_id) {
    char* c = nullptr;
    if (!nvctrl.XNVCTRLQueryTargetStringAttribute(display.get(), target_type, target_id, 0, attr, &c)) {
        std::cerr << "Failed to query attribute '" << attr << "'.\n";
    }
    return c;
}

void getNvctrlInfo(){
    string_map params;
    std::string token;

    if (!display)
        return;

    int enums[] = {
        NV_CTRL_STRING_GPU_UTILIZATION,
        NV_CTRL_STRING_GPU_CURRENT_CLOCK_FREQS,
        0 // keep null
    };

    for (size_t i=0; enums[i]; i++) {
        char* str = get_attr_target_string(enums[i], NV_CTRL_TARGET_TYPE_GPU, 0);
        if (!str)
            continue;

        std::stringstream ss (str);
        while (std::getline(ss, token, ',')) {
            parse_token(token, params);
        }
        free(str);
    }

    if (!try_stoi(nvctrl_info.load, params["graphics"]))
        nvctrl_info.load = 0;
    if (!try_stoi(nvctrl_info.CoreClock, params["nvclock"]))
        nvctrl_info.CoreClock = 0;
    if (!try_stoi(nvctrl_info.MemClock, params["memclock"]))
        nvctrl_info.MemClock = 0;

    int64_t temp = 0;
    nvctrl.XNVCTRLQueryTargetAttribute64(display.get(),
                        NV_CTRL_TARGET_TYPE_GPU,
                        0,
                        0,
                        NV_CTRL_GPU_CORE_TEMPERATURE,
                        &temp);
    nvctrl_info.temp = temp;

    int64_t memtotal = 0;
    nvctrl.XNVCTRLQueryTargetAttribute64(display.get(),
                        NV_CTRL_TARGET_TYPE_GPU,
                        0,
                        0,
                        NV_CTRL_TOTAL_DEDICATED_GPU_MEMORY,
                        &memtotal);
    nvctrl_info.memoryTotal = memtotal;

    int64_t memused = 0;
    nvctrl.XNVCTRLQueryTargetAttribute64(display.get(),
                        NV_CTRL_TARGET_TYPE_GPU,
                        0,
                        0,
                        NV_CTRL_USED_DEDICATED_GPU_MEMORY,
                        &memused);
    nvctrl_info.memoryUsed = memused;
}
