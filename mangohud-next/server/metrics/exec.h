#pragma once

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <sys/types.h>

#include <spdlog/spdlog.h>

#include "../common/helpers.hpp"

class Exec {
public:
    Exec();
    ~Exec();

    std::pair<bool, std::string> get(const std::string& command);

    static const char* value_command(std::string_view name) {
        static constexpr std::pair<std::string_view, const char*> commands[] = {
            {"KERNEL", "uname -r"},
            {"OS_NAME", "sed -n 's/PRETTY_NAME=\\(.*\\)/\\1/p' /etc/os-release | tr -d '\"'"},
            {"CPU_NAME", "sed -n 's/^model name.*: \\(.*\\)/\\1/p' /proc/cpuinfo | sed 's/([^)]*)//g' | tail -n1"},
            {"CPU_GOVERNOR", "cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"},
            {"ARCH",
             "exe=$(readlink /proc/{pid}/exe 2>/dev/null); "
             "case \"$exe\" in "
             "*preloader*|\"\") "
             "cwd=$(readlink /proc/{pid}/cwd 2>/dev/null); "
             "cmd=$(tr '\\0' '\\n' < /proc/{pid}/cmdline 2>/dev/null | awk '{l=tolower($0); if (l ~ /\\.exe/) {print; exit}}'); "
             "if [ -n \"$cmd\" ]; then "
             "case \"$cmd\" in "
             "/*) target=\"$cmd\" ;; "
             "*) base=${cmd##*/}; base=${base##*\\\\}; target=\"$cwd/$base\" ;; "
             "esac; "
             "else "
             "comm=$(cat /proc/{pid}/comm 2>/dev/null); "
             "for f in \"$cwd/$comm\"*; do [ -e \"$f\" ] && target=\"$f\" && break; done; "
             "[ -n \"$target\" ] || target=\"$cwd/$comm\"; "
             "fi ;; "
             "*) target=\"$exe\" ;; "
             "esac; "
             "info=$(file -Lb \"$target\" 2>/dev/null); "
             "case \"$info\" in "
             "*x86-64*|*x86_64*|*AMD64*) printf x86_64 ;; "
             "*80386*|*i386*|*Intel\\ 386*) printf x86 ;; "
             "*aarch64*|*AArch64*|*ARM64*) printf aarch64 ;; "
             "*ARM*) printf arm ;; "
             "*RISC-V*64-bit*) printf riscv64 ;; "
             "*RISC-V*32-bit*) printf riscv32 ;; "
             "*PowerPC*64-bit*) printf ppc64 ;; "
             "*PowerPC*) printf ppc ;; "
             "*PE32+*|*64-bit*) printf 64-bit ;; "
             "*PE32*|*32-bit*) printf 32-bit ;; "
             "*) printf UNKNOWN ;; "
             "esac"},
            {"DESKTOP_SESSION", "printf %s \"$XDG_CURRENT_DESKTOP\""},
            {"GAMEMODE", "grep -qi gamemode /proc/{pid}/maps && printf ON || printf OFF"},
            {"VKBASALT", "grep -qi vkbasalt /proc/{pid}/maps && printf ON || printf OFF"},
            {"WINESYNC",
             "out=NONE; "
             "for f in /proc/{pid}/fd/*; do "
             "link=$(readlink \"$f\" 2>/dev/null) || continue; "
             "case \"$link\" in "
             "*ntsync*|*NTSYNC*) out=NTsync; break ;; "
             "*fsync*|*FSYNC*) out=Fsync; break ;; "
             "*esync*|*ESYNC*) out=Esync; break ;; "
             "*winesync*|*WINESYNC*) out=Wserver; break ;; "
             "esac; "
             "done; "
             "printf %s \"$out\""},
            {"WINE_VERSION",
             "exe=$(readlink /proc/{pid}/exe 2>/dev/null); "
             "case \"${exe##*/}\" in wine-preloader|wine64-preloader) ;; *) exit 0 ;; esac; "
             "version_file=; "
             "case \"$exe\" in "
             "*/dist/bin/wine|*/files/bin/wine|*/dist/bin-wow64/wine|*/files/bin-wow64/wine) version_file=\"$(dirname \"$exe\")/../../version\" ;; "
             "*/files/lib/wine/*) version_file=\"$(dirname \"$exe\")/../../../../version\" ;; "
             "esac; "
             "if [ -n \"$version_file\" ] && [ -r \"$version_file\" ]; then "
             "version=$(awk '{print $2; exit}' \"$version_file\"); "
             "case \"$version\" in proton-*) version=\"Proton ${version#proton-}\" ;; ?*) version=\"Proton $version\" ;; esac; "
             "printf %s \"$version\"; exit 0; "
             "fi; "
             "dir=$(dirname \"$exe\"); "
             "bin=wine; "
             "[ \"${exe##*/}\" = wine64-preloader ] && bin=wine64; "
             "env -u WINELOADERNOEXEC \"$dir/$bin\" --version 2>/dev/null"},
            {"MEDIA_TITLE", "playerctl metadata title"},
            {"MEDIA_ALBUM", "playerctl metadata album"},
            {"MEDIA_ARTIST", "playerctl metadata artist"},
            {"CLIENT_EXE",
             "exe=$(readlink /proc/{pid}/exe 2>/dev/null); "
             "case \"$exe\" in "
             "*preloader*|\"\") "
             "cwd=$(readlink /proc/{pid}/cwd 2>/dev/null); "
             "cmd=$(tr '\\0' '\\n' < /proc/{pid}/cmdline 2>/dev/null | awk '{l=tolower($0); if (l ~ /\\.exe/) {print; exit}}'); "
             "if [ -n \"$cmd\" ]; then "
             "cmd=${cmd##*/}; cmd=${cmd##*\\\\}; printf %s \"$cmd\"; "
             "else "
             "comm=$(cat /proc/{pid}/comm 2>/dev/null); "
             "for f in \"$cwd/$comm\"*; do [ -e \"$f\" ] && basename \"$f\" && exit; done; "
             "printf %s \"$comm\"; "
             "fi ;; "
             "*) basename \"$exe\" ;; "
             "esac"},
        };

        for (const auto& [key, command] : commands) {
            if (key == name)
                return command;
        }

        return nullptr;
    }

private:
    struct Job {
        pid_t pid = -1;
        unique_fd stdout_fd;
        unique_fd stderr_fd;
        std::string stdout_output;
        std::string stderr_output;
        std::chrono::steady_clock::time_point started;

        Job(pid_t pid_, unique_fd stdout_fd_, unique_fd stderr_fd_,
            std::chrono::steady_clock::time_point started_);
        ~Job();
    };

    struct Entry {
        std::mutex m;
        std::string cached_output;
        std::string last_stderr_log;
        std::string last_spawn_error;
        std::chrono::steady_clock::time_point last_request;
        std::unique_ptr<Job> job;
        bool valid = false;

        bool running() const { return job != nullptr; }
    };

    using EntryList = std::vector<std::pair<std::string, std::shared_ptr<Entry>>>;

    std::thread thread;
    std::atomic<bool> stop {false};
    std::mutex m;
    std::unordered_map<std::string, std::shared_ptr<Entry>> entries;

    std::chrono::milliseconds interval{1000};
    std::chrono::milliseconds timeout{750};
    std::chrono::milliseconds stale_after{3000};
    static constexpr size_t stdout_limit = 4096;
    static constexpr size_t stderr_limit = 2048;

    void run();
    void tick();
    void remove_stale_entries(std::chrono::steady_clock::time_point now);
    void update_running_jobs(EntryList& active_entries,
                             std::chrono::steady_clock::time_point now);
    void drain_job(Job& job);
    void drain_fd(unique_fd& fd, std::string& output, size_t limit);
    void finish_job(const std::string& command, Entry* entry, std::unique_ptr<Job> job,
                    int status, bool timed_out);
    bool spawn(const std::string& command, Entry* entry);
    void log_spawn_error(const std::string& command, Entry& entry, const char* error);
};
