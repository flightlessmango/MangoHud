#include "logging.h"
#include "file_utils.h"
#include "string_utils.h"
#include "sysctl_utils.h"

void get_system_info() {
    unsigned long physmem = 0;
    if (sysctl_read("hw.physmem", physmem))
        ram = std::to_string(physmem / 1024); // kB, like MemTotal

    cpu = remove_parentheses(sysctl_read_string("hw.model"));
    trim(cpu);
    kernel = sysctl_read_string("kern.osrelease");
    os = sysctl_read_string("kern.ostype") + " " + kernel;
    cpusched.clear();
}
