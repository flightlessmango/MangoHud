#include <set>
#include <string>
#include <algorithm>
#include <spdlog/spdlog.h>
#include <filesystem.h>

#include "blacklist.h"
#include "file_utils.h"

namespace fs = ghc::filesystem;

static std::string get_proc_name() {
   // Note: It is possible to use GNU program_invocation_short_name.
   const std::string proc_name = get_wine_exe_name(/*keep_ext=*/true);
   if (!proc_name.empty()) {
       return proc_name;
   }
   return get_basename(get_exe_path());
}

// Assign global_proc_name once, and use its cached value, it is runtime-constant.
std::string global_proc_name = get_proc_name();
std::string global_engine_name;

static std::set<std::string> blacklist {
    "Amazon Games UI.exe",
    "Battle.net.exe",
    "BethesdaNetLauncher.exe",
    "EADesktop.exe",
    "EALauncher.exe",
    "EpicGamesLauncher.exe",
    "EpicWebHelper.exe",
    "explorer.exe",
    "ffxivlauncher.exe",
    "ffxivlauncher64.exe",
    "GalaxyClient.exe",
    "gamescope",
    "GardenGate_Launcher.exe",
    "gldriverquery",
    "halloy",
    "IGOProxy.exe",
    "IGOProxy64.exe",
    "iexplore.exe",
    "InsurgencyEAC.exe",
    "Launcher", //Paradox Interactive Launcher
    "LeagueClient.exe",
    "LeagueClientUxRender.exe",
    "MarneLauncher.exe",
    "MarvelRivals_Launcher.exe",
    "monado-service",
    "Origin.exe",
    "OriginThinSetupInternal.exe",
    "plutonium.exe",
    "plutonium-launcher-win32.exe",
    "REDlauncher.exe",
    "REDprelauncher.exe",
    "RSI Launcher.exe",
    "rundll32.exe",
    "SocialClubHelper.exe",
    "StarCitizen_Launcher.exe",
    "steam",
    "Steam.exe",
    "steamwebhelper",
    "steamwebhelper.exe",
    "tabtip.exe",
    "UplayWebCore.exe",
    "vrcompositor",
    "vulkandriverquery",
};

static std::set<std::string> blacklist_engine {
    "GTK"
};

static bool check_blacklisted() {
    bool blacklisted = blacklist.find(global_proc_name) != blacklist.end();
    bool blacklisted_engine = blacklist_engine.find(global_engine_name) != blacklist_engine.end();
    blacklisted |= blacklisted_engine;

    static bool printed = false;
    if(blacklisted && !printed) {
        printed = true;
        SPDLOG_INFO("process '{}' is blacklisted in MangoHud", global_proc_name);
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
    SPDLOG_DEBUG("add {} to blacklist", new_item);
    auto [_, inserted] = blacklist.insert(new_item);
    if (inserted) {
        // TODO: actually the checking should be done, after __all__ items have been added, right?
        is_blacklisted(true);
    }
}
