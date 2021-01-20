#include <vector>
#include <string>
#include <algorithm>

#include "blacklist.h"
#include "string_utils.h"
#include "file_utils.h"

static std::string get_proc_name() {
   // Note: It is possible to use GNU program_invocation_short_name.
   const std::string proc_name = get_wine_exe_name(/*keep_ext=*/true);
   if (!proc_name.empty()) {
       return proc_name;
   }
   const std::string p = get_exe_path();
   return p.substr(p.find_last_of("/\\") + 1);
}

static  std::vector<std::string> blacklist {
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


static bool check_blacklisted() {
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

void add_blacklist(const std::string& new_item) {
    // check if item exits in blacklist before adding new item
    if(std::find(blacklist.begin(), blacklist.end(), new_item) != blacklist.end()) {
        return;
    }

    blacklist.push_back (new_item);
    is_blacklisted(true);
}


