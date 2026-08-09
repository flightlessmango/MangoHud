#pragma once
#include "obs_plugin.h"
class ObsStudio{
    public:
        ObsStudio(bool prefix_exe, const char* procname);
        static ObsStats* stats;
        static int isinit;
        static int islogged_obsunavailable;
        static void update();
        static void atexit_func();
        static char col1[16];
        static char col2[16];
};
