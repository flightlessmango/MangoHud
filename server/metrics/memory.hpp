#pragma once

std::map<std::string, float> get_ram_info();
std::map<std::string, float> get_process_memory(pid_t pid);
