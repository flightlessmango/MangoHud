#include <array>
#include <vector>
#include <sstream>
#include <iostream>
#include <string>
#include <mutex>
#include <tuple>

#include "nvidia_info.h"
#include "memory.h"
#include "cpu.h"
#include "async.h"

using namespace std;

int gpuLoad = 0, gpuTemp = 0, cpuTemp = 0;
FILE *amdGpuFile = nullptr, *amdTempFile = nullptr, *cpuTempFile = nullptr, *amdGpuVramTotalFile = nullptr, *amdGpuVramUsedFile = nullptr;
float gpuMemUsed = 0, gpuMemTotal = 0;

int numCpuCores = std::thread::hardware_concurrency();
pthread_t cpuThread, gpuThread, cpuInfoThread;

string exec(string command) {
   char buffer[128];
   string result = "";

   // Open pipe to file
   FILE* pipe = popen(command.c_str(), "r");
   if (!pipe) {
      return "popen failed!";
   }

   // read till end of process:
   while (!feof(pipe)) {

      // use buffer to read and add to result
      if (fgets(buffer, 128, pipe) != NULL)
         result += buffer;
   }

   pclose(pipe);
   return result;
}

struct gpu_stats {
    virtual ~gpu_stats(){}
    typedef std::tuple<bool, int, int, float, float> my_type;
    virtual my_type get() = 0;
    auto run ()
    {
        return runAsyncAndCatch<my_type>(&gpu_stats::get, this);
    }
protected:
    std::mutex m;
};

struct async_get_int {
    virtual ~async_get_int(){}
    typedef std::tuple<bool, int> my_type;
    virtual my_type get() = 0;
    auto run ()
    {
        return runAsyncAndCatch<my_type>(&async_get_int::get, this);
    }
protected:
    std::mutex m;
};

struct cpu_stats_updater {
    const CPUData& GetCPUDataTotal() const {
        return stats.GetCPUDataTotal();
    }

    const std::vector<CPUData>& GetCPUData() const {
        return stats.GetCPUData();
    }

    bool get() {
        std::lock_guard<std::mutex> lk(m);
        return stats.UpdateCPUData();
    }

    auto run () {
        return runAsyncAndCatch<bool>(&cpu_stats_updater::get, this);
    }
protected:
    CPUStats stats;
    std::mutex m;
};

struct cpu_temp : public async_get_int {
    cpu_temp(FILE* h) : handle(h) {};
    my_type get()
    {
        std::lock_guard<std::mutex> lk(m);
        int cpuTemp = 0;
        rewind(handle);
        fflush(handle);
        if (fscanf(handle, "%d", &cpuTemp) != 1)
            cpuTemp = 0;
        cpuTemp /= 1000;
        return my_type(true, cpuTemp);
    }
private:
    FILE* handle = nullptr;
};

struct nvidia_gpu_stats : public gpu_stats
{
    my_type get()
    {
        std::lock_guard<std::mutex> lk(m);
        int load = 0, temp = 0;
        float used = 0, total = 0;
        (void)total;

        bool ret = false;
        if (checkNvidia()){
            ret = getNvidiaInfo(load, temp, used);
        }
        return my_type(ret, load, temp, used, 0);
    }
};

struct amdgpu_gpu_stats : public gpu_stats {

    amdgpu_gpu_stats (FILE *busy, FILE *temp, FILE *used, FILE *total)
    : hbusy(busy)
    , htemp(temp)
    , hused(used)
    //, htotal(total)
    {
        if (total) {
            rewind(total);
            fflush(total);
            if (fscanf(total, "%" PRId64, &memTotal) != 1)
                memTotal = 0;
            memTotal /= (1024 * 1024);
            fclose(total);
        }
    }

    my_type get()
    {
        std::lock_guard<std::mutex> lk(m);
        int load, temp;
        int64_t used, total = memTotal;

        if (hbusy) {
            rewind(hbusy);
            fflush(hbusy);
            if (fscanf(hbusy, "%d", &load) != 1)
                load = 0;
        }

        if (htemp) {
            rewind(htemp);
            fflush(htemp);
            if (fscanf(htemp, "%d", &temp) != 1)
                temp = 0;
            temp /= 1000;
        }

        if (hused) {
            rewind(hused);
            fflush(hused);
            if (fscanf(hused, "%" PRId64, &used) != 1)
                used = 0;
            used /= (1024.f * 1024.f);
        }
        return my_type(true, load, temp, used, total);
    }
private:
    int64_t memTotal = 0;
    FILE *hbusy = nullptr, *htemp = nullptr;
    FILE *hused = nullptr, *htotal = nullptr;
};
