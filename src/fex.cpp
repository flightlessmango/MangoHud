#include <spdlog/spdlog.h>
#include <map>
#include <string>
#include <unistd.h>
#include <sys/mman.h>

#include "fex.h"
#include "hud_elements.h"
#include "mesa/util/macros.h"
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

namespace fex {
const char* fex_status = "Not Found!";
std::string fex_version;
std::vector<float> fex_load_data(200,0.f);

fex_event_counts sigbus_counts;
fex_event_counts smc_counts;
fex_event_counts softfloat_counts;

std::vector<float> fex_max_thread_loads;

constexpr static uint32_t MAXIMUM_THREAD_WAIT_TIME = 10;

// FEX-Emu stats definitions
// Semantically these match upstream FEX-Emu.
constexpr uint32_t FEX_STATS_VERSION = 2;
enum class AppType : uint8_t {
    LINUX_32,
    LINUX_64,
    WIN_ARM64EC,
    WIN_WOW64,
};

// The profile statistics header that is at the base of the shared memory mapped from FEX.
// The version member is guaranteed to be first, to ensure that any version changes can be picked up immediately.
struct fex_stats_header {
    uint8_t Version;
    AppType app_type;
    uint16_t thread_stats_size;
    char fex_version[48];
    // Atomic variables. std::atomic_ref isn't available until C++20, so need to use GCC builtin atomics to access.
    uint32_t Head;
    uint32_t Size;
    uint32_t Pad;
};

// The thread-specific datapoints. If TID is zero then it is deallocated and happens to still be in the linked list.
struct fex_thread_stats {
    // Atomic variables.
    uint32_t Next;
    uint32_t TID;
    // Thread-specific stats.
    uint64_t AccumulatedJITTime;
    uint64_t AccumulatedSignalTime;
    uint64_t AccumulatedSIGBUSCount;
    uint64_t AccumulatedSMCEvents;
    uint64_t AccumulatedFloatFallbackCount;
};

// This is guaranteed by FEX.
static_assert(sizeof(fex_thread_stats) % 16 == 0, "");

// Sampled stats information
struct fex_stats {
    int pid {-1};
    int shm_fd {-1};
    bool first_sample = true;
    uint32_t shm_size{};
    uint64_t cycle_counter_frequency{};
    size_t hardware_concurrency{};
    size_t page_size{};

    void* shm_base{};
    size_t fex_thread_stats_size {};
    fex_stats_header* head{};
    fex_thread_stats* stats{};

    struct retained_stats {
        std::chrono::time_point<std::chrono::steady_clock> last_seen{};
        fex_thread_stats previous{};
        fex_thread_stats current{};
    };
    std::chrono::time_point<std::chrono::steady_clock> previous_sample_period;
    std::map<int, retained_stats> sampled_stats;
};

fex_stats g_stats {};

const char* get_fex_app_type() {
    if (!g_stats.head) {
        return "Unknown";
    }

    // These are the only application types that FEX-Emu supports today.
    // Linux32: A 32-bit x86 Linux application
    // Linux64: A 64-bit x86_64 Linux application
    // arm64ec: A 64-bit x86_64 WINE application
    //   wow64: A 32-bit x86 WINE application
    switch (g_stats.head->app_type) {
        case AppType::LINUX_32: return "Linux32";
        case AppType::LINUX_64: return "Linux64";
        case AppType::WIN_ARM64EC: return "arm64ec";
        case AppType::WIN_WOW64: return "wow64";
        default: return "Unknown";
    }
}

static fex_thread_stats *offset_to_stats(void* shm_base, uint32_t *offset) {
    const auto ld = __atomic_load_n(offset, __ATOMIC_RELAXED);
    if (ld == 0) return nullptr;
    return reinterpret_cast<fex_thread_stats*>(reinterpret_cast<uint64_t>(shm_base) + ld);
}

static fex_thread_stats *offset_to_stats(void* shm_base, uint32_t offset) {
    if (offset == 0) return nullptr;
    return reinterpret_cast<fex_thread_stats*>(reinterpret_cast<uint64_t>(shm_base) + offset);
}

#ifdef __aarch64__
static void memory_barrier() {
    asm volatile("dmb ishst" ::: "memory");
}
static uint64_t get_cycle_counter_frequency() {
    uint64_t result;
    asm ("mrs %[res], CNTFRQ_EL0;"
        : [res] "=r" (result));
    return result;
}
bool is_fex_capable() {
    // All aarch64 systems are fex capable.
    return true;
}

#elif defined(__x86_64__) || defined(__i386__)
static void memory_barrier() {
    // Intentionally empty.
}
static void cpuid(uint32_t leaf, uint32_t &eax, uint32_t &ebx, uint32_t &ecx, uint32_t &edx) {
    asm volatile("cpuid"
            : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
            : "a"(leaf), "c"(0));
}

bool is_fex_capable() {
    // FEX-Emu CPUID documentation: https://github.com/FEX-Emu/FEX/blob/main/docs/CPUID.md
    const uint32_t HYPERVISOR_BIT = 1U << 31;
    const char FEXHypervisorString[] = "FEXIFEXIEMU";
    char HypervisorString[4 * 3];

    uint32_t eax, ebx, ecx, edx;
    // Check that the hypervisor bit is set first. Not required, but good to do.
    cpuid(1, eax, ebx, ecx, edx);
    if ((ecx & HYPERVISOR_BIT) != HYPERVISOR_BIT) return false;

    // Once the hypervisor bit is set, query the hypervisor leaf.
    cpuid(0x4000'0000U, eax, ebx, ecx, edx);
    if (eax == 0) return false;

    // If the hypervisor description matches FEX then we're good.
    memcpy(&HypervisorString[0], &ebx, sizeof(uint32_t));
    memcpy(&HypervisorString[4], &ecx, sizeof(uint32_t));
    memcpy(&HypervisorString[8], &edx, sizeof(uint32_t));
    if (strncmp(HypervisorString, FEXHypervisorString, sizeof(HypervisorString)) != 0) return false;

    return true;
}

static uint64_t get_cycle_counter_frequency() {
    // In a FEX-Emu environment, the cycle counter frequency is exposed in CPUID leaf 0x15.
    // This matches x86 Intel semantics on latest CPUs, see their documentation for the exact implementation details.
    uint32_t eax, ebx, ecx, edx;
    cpuid(0, eax, ebx, ecx, edx);
    if (eax < 0x15) return 0;

    cpuid(0x15U, eax, ebx, ecx, edx);

    // Ignore scale in ebx
    // Baseline clock is provided in ecx.
    return ecx;
}
#endif

static void atomic_copy_thread_stats(fex_thread_stats *dest, const fex_thread_stats *src) {
#if defined(__x86_64__) || defined(__i386__)
    // For x86 platforms, XMM copies are atomic when aligned. So this guarantees single-copy atomicity.
    // x86 has no equivalent of an true "atomic" 128-bit GPR loadstore until APX.
    // For FEX emulating x86 platforms, this is also a guarantee for ARMv8.4 and newer.
    using copy_type = __m128;
#else
    // For ARM64 platforms this is basically guaranteed to turn in to ldp+stp.
    // For ARM8.4 this gives us single-copy atomicity guarantees.
    using copy_type = __uint128_t;
#endif

    const auto elements_to_copy = g_stats.fex_thread_stats_size / sizeof(copy_type);
    auto d_i = reinterpret_cast<copy_type*>(dest);
    auto s_i = reinterpret_cast<const copy_type*>(src);
    for (size_t i = 0; i < elements_to_copy; ++i) {
        d_i[i] = s_i[i];
    }
}

static void destroy_shm() {
    munmap(g_stats.shm_base, g_stats.shm_size);
    close(g_stats.shm_fd);
    g_stats.shm_fd = -1;
    g_stats.shm_size = 0;
    g_stats.shm_base = nullptr;
    g_stats.head = nullptr;
    g_stats.stats = nullptr;
    g_stats.sampled_stats.clear();
}

static void init_shm(int pid) {
    if (g_stats.shm_fd != -1) {
        // Destroy first if the FD changed.
        destroy_shm();
    }

    // Initialize global hardware stats.
    g_stats.cycle_counter_frequency = get_cycle_counter_frequency();
    g_stats.hardware_concurrency = std::thread::hardware_concurrency();
    g_stats.page_size = sysconf(_SC_PAGESIZE);
    if (g_stats.page_size <= 0) g_stats.page_size = 4096;

    // Try and open a FEX stats file that relates to the PID in focus.
    // If this fails then it is non-fatal, just means FEX isn't creating stats for that process.
    std::string f = "fex-";
    f += std::to_string(pid);
    f += "-stats";
    int fd {-1};
    struct stat buf{};
    uint64_t shm_size{};
    void* shm_base{MAP_FAILED};
    fex_stats_header *header{};

    fd = shm_open(f.c_str(), O_RDONLY, 0);
    if (fd == -1) {
        goto err;
    }

    if (fstat(fd, &buf) == -1) {
        goto err;
    }

    if (buf.st_size < static_cast<off_t>(sizeof(fex_stats_header))) {
        goto err;
    }

    shm_size = ALIGN_POT(buf.st_size, g_stats.page_size);

    shm_base = mmap(nullptr, shm_size, PROT_READ, MAP_SHARED, fd, 0);
    if (shm_base == MAP_FAILED) {
        goto err;
    }

    memory_barrier();
    header = reinterpret_cast<fex_stats_header*>(shm_base);
    if (header->Version != FEX_STATS_VERSION) {
        // If the version read doesn't match the implementation then we can't read.
        fex_status = "version mismatch";
        goto err;
    }

    // Cache off the information, we have successfully loaded the stats SHM.
    g_stats.pid = pid;
    g_stats.shm_fd = fd;
    g_stats.shm_size = shm_size;
    g_stats.shm_base = shm_base;
    g_stats.head = header;
    g_stats.stats = offset_to_stats(shm_base, &header->Head);
    g_stats.previous_sample_period = std::chrono::steady_clock::now();
    g_stats.first_sample = true;
    g_stats.sampled_stats.clear();

    g_stats.fex_thread_stats_size = sizeof(fex_thread_stats);

    if (g_stats.head->thread_stats_size) {
        // If thread stats size is provided, use that, as long as it is smaller than tracking size.
        g_stats.fex_thread_stats_size = std::min<size_t>(g_stats.head->thread_stats_size, g_stats.fex_thread_stats_size);
    }

    fex_version = std::string {header->fex_version, strnlen(header->fex_version, sizeof(header->fex_version))};
    sigbus_counts.account_time(g_stats.previous_sample_period);
    smc_counts.account_time(g_stats.previous_sample_period);
    softfloat_counts.account_time(g_stats.previous_sample_period);
    std::fill(fex_load_data.begin(), fex_load_data.end(), 0.0);
    fex_max_thread_loads.clear();
    return;
err:
    if (fd != -1) {
        close(fd);
    }

    if (shm_base != MAP_FAILED) {
        munmap(shm_base, shm_size);
    }
}

static void check_shm_update_necessary() {
    // If the SHM has changed size then we need to unmap and remap with the new size.
    // Required since FEX may grow the SHM region to fit more threads, although previous thread data won't be invalidated.
    memory_barrier();
    auto new_shm_size = ALIGN_POT(__atomic_load_n(&g_stats.head->Size, __ATOMIC_RELAXED), g_stats.page_size);
    if (g_stats.shm_size == new_shm_size) {
        return;
    }

    munmap(g_stats.shm_base, g_stats.shm_size);
    g_stats.shm_size = new_shm_size;
    g_stats.shm_base = mmap(nullptr, new_shm_size, PROT_READ, MAP_SHARED, g_stats.shm_fd, 0);
    g_stats.head = reinterpret_cast<fex_stats_header*>(g_stats.shm_base);
    g_stats.stats = offset_to_stats(g_stats.shm_base, &g_stats.head->Head);
    g_stats.fex_thread_stats_size = sizeof(fex_thread_stats);

    if (g_stats.head->thread_stats_size) {
        // If thread stats size is provided, use that, as long as it is smaller than tracking size.
        g_stats.fex_thread_stats_size = std::min<size_t>(g_stats.head->thread_stats_size, g_stats.fex_thread_stats_size);
    }
}

bool is_fex_pid_found() {
    return g_stats.pid != -1;
}

void update_fex_stats() {
    auto gs_pid = HUDElements.g_gamescopePid > 0 ? HUDElements.g_gamescopePid : ::getpid();
    if (gs_pid < 1) {
        // No PID yet.
        return;
    }

    if (g_stats.pid != gs_pid) {
        // PID changed, likely gamescope changed focus.
        init_shm(gs_pid);
    }

    if (g_stats.pid == -1) {
        // PID became invalid. Likely due to error reading SHM.
        return;
    }

    // Check if SHM changed first.
    check_shm_update_necessary();

    // Before reading stats, a memory barrier needs to be done.
    // This ensures visibility of the stats before reading, as they use weak atomics for writes.
    memory_barrier();

    // Sample the stats and store them off.
    // Sampling these quickly lets us become a loose sampling profiler, since FEX updates these constantly.
    uint32_t *header_offset_atomic = &g_stats.head->Head;
    auto now = std::chrono::steady_clock::now();
    for (auto header_offset = __atomic_load_n(header_offset_atomic, __ATOMIC_RELAXED);
         header_offset != 0;
         header_offset = __atomic_load_n(header_offset_atomic, __ATOMIC_RELAXED)) {
        if (header_offset >= g_stats.shm_size) break;

        fex_thread_stats *stat = offset_to_stats(g_stats.shm_base, header_offset);
        const auto TID = __atomic_load_n(&stat->TID, __ATOMIC_RELAXED);
        if (TID != 0) {
            fex_stats::retained_stats &sampled_stats = g_stats.sampled_stats[TID];
            atomic_copy_thread_stats(&sampled_stats.current, stat);
            sampled_stats.last_seen = now;
        }

        header_offset_atomic = &stat->Next;
    }

    if (g_stats.first_sample) {
        // Skip first sample, it'll look crazy.
        g_stats.first_sample = false;
        fex_status = "Accumulating";
        return;
    }

    // Update the status with the FEX version.
    fex_status = fex_version.c_str();

    // Accumulate full JIT time
    uint64_t total_jit_time{};
    uint64_t total_sigbus_events{};
    uint64_t total_smc_events{};
    uint64_t total_softfloat_events{};
    size_t threads_sampled{};
#define accumulate(dest, name) dest += it->second.current.name - it->second.previous.name
    std::vector<uint64_t> hottest_threads{};
    for (auto it = g_stats.sampled_stats.begin(); it != g_stats.sampled_stats.end();) {
        ++threads_sampled;
        uint64_t total_time{};
        accumulate(total_time, AccumulatedJITTime);
        accumulate(total_time, AccumulatedSignalTime);
        accumulate(total_sigbus_events, AccumulatedSIGBUSCount);
        accumulate(total_smc_events, AccumulatedSMCEvents);
        accumulate(total_softfloat_events, AccumulatedFloatFallbackCount);

        memcpy(&it->second.previous, &it->second.current, g_stats.fex_thread_stats_size);

        total_jit_time += total_time;
        if ((now - it->second.last_seen) >= std::chrono::seconds(MAXIMUM_THREAD_WAIT_TIME)) {
            it = g_stats.sampled_stats.erase(it);
            continue;
        }
        hottest_threads.emplace_back(total_time);
        ++it;
    }

    std::sort(hottest_threads.begin(), hottest_threads.end(), std::greater<uint64_t>());

    // Calculate loads based on the sample period that occurred.
    // FEX-Emu only counts cycles for the amount of time, so we need to calculate load based on the number of cycles that the sample period has.
    const auto sample_period = now - g_stats.previous_sample_period;

    const double NanosecondsInSeconds = 1'000'000'000.0;
    const double SamplePeriodNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(sample_period).count();
    const double MaximumCyclesInSecond = (double)g_stats.cycle_counter_frequency;
    const double MaximumCyclesInSamplePeriod = MaximumCyclesInSecond * (SamplePeriodNanoseconds / NanosecondsInSeconds);
    const double MaximumCoresThreadsPossible = std::min(g_stats.hardware_concurrency, threads_sampled);

    // Calculate the percentage of JIT time that could possibly exist inside the sample period.
    double fex_load = ((double)total_jit_time / (MaximumCyclesInSamplePeriod * MaximumCoresThreadsPossible)) * 100.0;
    size_t minimum_hot_threads = std::min(g_stats.hardware_concurrency, hottest_threads.size());
    // For the top thread-loads, we are only ever showing up to how many hardware threads are available.
    fex_max_thread_loads.resize(minimum_hot_threads);
    for (size_t i = 0; i < minimum_hot_threads; ++i) {
       fex_max_thread_loads[i] = ((double)hottest_threads[i] / MaximumCyclesInSamplePeriod) * 100.0;
    }

    sigbus_counts.account(total_sigbus_events, now);
    smc_counts.account(total_smc_events, now);
    softfloat_counts.account(total_softfloat_events, now);

    g_stats.previous_sample_period = now;

    fex_load_data.push_back(fex_load);
    fex_load_data.erase(fex_load_data.begin());
}
}
