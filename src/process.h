#pragma once
#ifndef MANGOHUD_PROCESS_H
#define MANGOHUD_PROCESS_H

/**
 * Checks if a process whose command string contains process_name is running
 * 
 * @param process_name Name of the process
 * @return true if process is running, otherwise false
 */
bool process_running(const char *process_name);

/**
 * Returns the PID of the first process whose command string contains process_name
 * 
 * @param process_name Name of the process
 * @return PID of the process or -1 if not found
 */
int process_id(const char *process_name);

#endif // MANGOHUD_PROCESS_H