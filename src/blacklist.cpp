#include <vector>
#include <string>
#include <algorithm>

#include "blacklist.h"
#include "string_utils.h"
#include "file_utils.h"

static std::string get_proc_name() {
#ifdef _GNU_SOURCE_OFF
   std::string p(program_invocation_name);
   std::string proc_name = p.substr(p.find_last_of("/\\") + 1);
#else
   std::string p = get_exe_path();
   std::string proc_name;
   if (ends_with(p, "wine-preloader") || ends_with(p, "wine64-preloader")) {
      get_wine_exe_name(proc_name, true);
   } else {
      proc_name = p.substr(p.find_last_of("/\\") + 1);
   }
#endif
    return proc_name;
}

static bool check_blacklisted() {
    static const std::vector<std::string> blacklist {
        "Battle.net.exe",
        "BethesdaNetLauncher.exe",
        "EpicGamesLauncher.exe",
        "IGOProxy.exe",
        "IGOProxy64.exe",
        "Origin.exe",
        "OriginThinSetupInternal.exe",
        "steam",
        "steamwebhelper",
        "gldriverquery",
        "vulkandriverquery",
        "Steam.exe",
        "ffxivlauncher.exe",
        "ffxivlauncher64.exe",
        "LeagueClient.exe",
        "LeagueClientUxRender.exe",
        "SocialClubHelper.exe",
    };

    std::string proc_name = get_proc_name();
    bool blacklisted = std::find(blacklist.begin(), blacklist.end(), proc_name) != blacklist.end();

    if(blacklisted) {
        fprintf(stderr, "INFO: process %s is blacklisted in MangoHud\n", proc_name.c_str());
    }

    return blacklisted;
}

bool is_blacklisted(bool force_recheck) {
    static bool blacklisted = check_blacklisted();
    if (force_recheck)
        blacklisted = check_blacklisted();
    return blacklisted;
}
