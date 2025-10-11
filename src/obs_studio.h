#pragma once
#include "obs_plugin.h"
class ObsStudio{
    public:
        static ObsStats* stats;
        static void atexit_func();
};
