#include <nlohmann/json.hpp>

#include <dirent.h>
#include <iostream>
#include <libdrm/i915_drm.h>
#include <linux/perf_event.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <cinttypes>

// cut down from file_utils.cpp
static std::vector<std::string> lsdir(const char *root, const char *prefix) {
  std::vector<std::string> list;
  struct dirent *dp;

  DIR *dirp = opendir(root);
  if (!dirp) {
    return list;
  }

  while ((dp = readdir(dirp))) {
    if ((prefix && !std::string(dp->d_name).find(prefix) == 0) ||
        !strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
      continue;

    switch (dp->d_type) {
    case DT_DIR:
      list.push_back(dp->d_name);
      break;
    }
  }

  closedir(dirp);
  return list;
}

static inline int perf_event_open(struct perf_event_attr *attr, pid_t pid,
                                  int cpu, int group_fd, unsigned long flags) {
  attr->size = sizeof(*attr);
  return syscall(SYS_perf_event_open, attr, pid, cpu, group_fd, flags);
}

static uint64_t i915_perf_device_type(const char *bus_id) {
  FILE *fd;
  char buf[80] = {0};
  uint64_t type;

  bool is_igpu = strcmp(bus_id, "0000_00_02.0") == 0; // Ref: igt-gpu-tools for magic.
  const char *dgpu_path = "/sys/bus/event_source/devices/i915_%s/type";
  const char *igpu_path = "/sys/bus/event_source/devices/i915/type";

  snprintf(buf, sizeof(buf), is_igpu ? igpu_path : dgpu_path, bus_id);
  if ((fd = fopen(buf, "r")) == nullptr)
    return 0;

  if (fscanf(fd, "%" PRIu64, &type) != 1)
    type = 0;

  fclose(fd);
  return type;
}

static inline int i915_perf_event_open(uint64_t type, uint64_t config) {
  uint64_t format = PERF_FORMAT_TOTAL_TIME_ENABLED;
  uint64_t group = -1;
  struct perf_event_attr attr = {0};

  attr.type = type;
  attr.read_format = format;
  attr.config = config;
  attr.use_clockid = 1;
  attr.clockid = CLOCK_MONOTONIC;

  int cpu = 0, ncpu = 16; // hopefully one of these works.
  int ret;
  do {
    ret = perf_event_open(&attr, -1, cpu++, group, 0);
  } while ((ret < 0 && errno == EINVAL) &&
           (cpu < ncpu)); // find a cpu to open on.

  return ret;
}

static int power_perf_event_open(const char *metric) {
  FILE *fd;
  char buf[80] = {0};
  uint64_t type, config;

  snprintf(buf, sizeof(buf), "/sys/devices/power/type");
  if ((fd = fopen(buf, "r")) == nullptr)
    return -1;

  int scanned = fscanf(fd, "%" PRIu64, &type);
  fclose(fd);
  if (scanned != 1)
    return -1;

  snprintf(buf, sizeof(buf), "/sys/devices/power/events/%s", metric);
  if ((fd = fopen(buf, "r")) == nullptr)
    return -1;

  scanned = fscanf(fd, "event=%" PRIx64, &config);
  fclose(fd);
  if (scanned != 1)
    return -1;

  return i915_perf_event_open(type, config);
}

struct counter {
  union {
    int fd;
    FILE *fp;
  };
  uint64_t val;
  uint64_t ts;
  uint64_t val_prev;
  uint64_t ts_prev;
};

static void counter_update(struct counter *c, uint64_t in[4]) {
  c->val_prev = c->val;
  c->ts_prev = c->ts;
  c->val = in[0];
  c->ts = in[1];
}

static float counter_value(struct counter c, float scale) {
  if (c.val < c.val_prev || c.ts_prev == 0) { // just try again next time.
    return 0.0;
  }
  float t = c.ts - c.ts_prev;
  float d = c.val - c.val_prev;
  return d / t * scale;
}

static std::string find_pci_busid(const char *drm_dev) {
  char drm_link_c[PATH_MAX] = {0};
  ssize_t count = readlink((std::string("/sys/class/drm/") + drm_dev).c_str(),
                           drm_link_c, PATH_MAX);
  if (count <= 0) {
    fprintf(stderr, "Invalid drm device: %s\n", drm_dev);
    exit(1);
  }
  std::string drm_link(drm_link_c);
  // Example link format:
  // ../../devices/pci0000:00/0000:00:03.1/0000:07:00.0/0000:08:01.0/0000:09:00.0/drm/card0
  ssize_t pci_start = drm_link.rfind("/", count - 14) + 1;
  ssize_t pci_end = drm_link.find("/", pci_start);
  return drm_link.substr(pci_start, pci_end - pci_start);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Not enough arguments. usage: mango_intel_stats \"card0\"\n");
    exit(1);
  }
  char *drm_dev = argv[1];
  std::string pci_path = find_pci_busid(drm_dev);
  std::replace(pci_path.begin(), pci_path.end(), ':',
               '_'); // just sysfs things.
  uint64_t type = i915_perf_device_type(pci_path.c_str());

  // Counters for this tool.
  struct counter busy_gpu, freq_gpu, energy_dgpu, energy_igpu, energy_cpu;

  freq_gpu.fd = i915_perf_event_open(type, I915_PMU_ACTUAL_FREQUENCY);
  if (errno == EACCES) {
    fprintf(
        stderr,
        "Permission denied on perf events.\nThis binary is meant to have "
        "CAP_PERFMON\n via: sudo setcap cap_perfmon=+ep mango_intel_stats\n");
    exit(1);
  }
  busy_gpu.fd = i915_perf_event_open(
      type, I915_PMU_ENGINE_BUSY(I915_ENGINE_CLASS_RENDER, 0));
  energy_cpu.fd = power_perf_event_open("energy-pkg");
  energy_igpu.fd = power_perf_event_open("energy-gpu");

  energy_dgpu = {0};
  std::string hwmon_path =
      std::string("/sys/class/drm/") + drm_dev + "/device/hwmon/";
  const auto dirs = lsdir(hwmon_path.c_str(), "hwmon");
  for (const auto &dir : dirs) {
    FILE *fp = fopen((hwmon_path + dir + "/energy1_input").c_str(), "r");
    if (fp) {
      energy_dgpu.fp = fp;
      break;
    }
  }

  while (true) {
    nlohmann::json j;
    uint64_t res[4] = {0};
    if (read(busy_gpu.fd, res, sizeof(res)) > 0) {
      counter_update(&busy_gpu, res);
      j["gpu_busy_pct"] = counter_value(busy_gpu, 100.0);
    }
    if (read(freq_gpu.fd, res, sizeof(res)) > 0) {
      counter_update(&freq_gpu, res);
      j["gpu_clock_mhz"] = counter_value(freq_gpu, 1e9);
    }
    if (read(energy_cpu.fd, res, sizeof(res)) > 0) {
      counter_update(&energy_cpu, res);
      // https://lwn.net/Articles/573602/, in practice intel and amd use 1/2^32J
      j["cpu_power_w"] = counter_value(energy_cpu, 2.328e-1);
    }
    if (read(energy_igpu.fd, res, sizeof(res)) > 0) {
      counter_update(&energy_igpu, res);
      j["igpu_power_w"] = counter_value(energy_igpu, 2.328e-1);
    }
    if (energy_dgpu.fp && fscanf(energy_dgpu.fp, "%" PRIu64, res) == 1) {
      rewind(energy_dgpu.fp);
      fflush(energy_dgpu.fp);
      // We can just depend on old clock readings in res[1]
      /*
      struct timespec t = {0};
      clock_gettime(CLOCK_MONOTONIC, &t);
      res[1] = (t.tv_sec % (3600 * 24)) * 1e9 + t.tv_nsec;
      */
      counter_update(&energy_dgpu, res);
      j["dgpu_power_w"] = counter_value(energy_dgpu, 1e3);
    }
    std::cout << j << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
}
