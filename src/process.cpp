#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include "process.h"

bool process_running(const char *process_name) {
	return process_id(process_name) != -1;
}

int process_id(const char *process_name) {
	int pid = -1;

	DIR *proc_dir = opendir("/proc");
	if (proc_dir == NULL)
		return pid;

	struct dirent *files;
	while ((files = readdir(proc_dir)) != NULL) {
		char process_filename[128];
		char process_cmd[256];

		snprintf(process_filename, 127, "/proc/%s/cmdline", files->d_name);
		FILE *process_file = fopen(process_filename, "r");
		if (process_file == NULL)
			continue;

		fgets(process_cmd, 255, process_file);
		fclose(process_file);
		if (strcasestr(process_cmd, process_name) != NULL) {
			pid = atoi(files->d_name);
			break;
		}
	}

	return pid;
}