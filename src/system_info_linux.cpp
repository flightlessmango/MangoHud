#include <algorithm>
#include "logging.h"
#include "file_utils.h"
#include "string_utils.h"

void get_system_info() {
    ram =  exec("sed -n 's/^MemTotal: *\\([0-9]*\\).*/\\1/p' /proc/meminfo");
    trim(ram);
    cpu =  exec("sed -n 's/^model name.*: \\(.*\\)/\\1/p' /proc/cpuinfo | sed 's/([^)]*)//g' | tail -n1");
    trim(cpu);
    kernel = exec("uname -r");
    trim(kernel);
    os = exec("sed -n 's/PRETTY_NAME=\\(.*\\)/\\1/p' /etc/os-release");
    os.erase(remove(os.begin(), os.end(), '\"' ), os.end());
    trim(os);
    cpusched = read_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
}
