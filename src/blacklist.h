#pragma once
#ifndef MANGOHUD_BLACKLIST_H
#define MANGOHUD_BLACKLIST_H
#include<string>
bool is_blacklisted(bool force_recheck = false);
void add_blacklist(const std::string& proc);
extern std::string global_proc_name;
extern std::string global_engine_name;

#endif //MANGOHUD_BLACKLIST_H
