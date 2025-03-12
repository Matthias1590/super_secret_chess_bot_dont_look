#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>

#define MAX_PATH 1024

// Function to check if the process name matches the given process name
int match_process_name(const char *pid, const char *process_name) {
    char path[MAX_PATH];
    FILE *fp;
    char cmdline[MAX_PATH];

    // Construct the path to the process's cmdline
    snprintf(path, sizeof(path), "/proc/%s/cmdline", pid);

    // Open the cmdline file for the process
    fp = fopen(path, "r");
    if (fp == NULL) {
        return 0; // Couldn't open the cmdline file
    }

    // Read the command line of the process
    if (fgets(cmdline, sizeof(cmdline), fp) != NULL) {
        // Compare the process name with the cmdline (just the first word)
        if (strstr(cmdline, process_name) != NULL) {
            return 1; // Process name matches
        }
    }

    return 0; // Process name does not match
}

pid_t find_pid_by_name(const char *process_name) {
    DIR *dir;
    pid_t current_pid = getpid();
    struct dirent *entry;

    // Open the /proc directory to look at process directories
    dir = opendir("/proc");
    if (dir == NULL) {
        return 0;
    }

    // Read each entry in the /proc directory
    while ((entry = readdir(dir)) != NULL) {
        // Check if the entry is a directory and represents a PID (numeric)
        if (entry->d_type == DT_DIR && atoi(entry->d_name) > 0 && atoi(entry->d_name) != current_pid) {
            // Check if the process name matches the given process name
            if (match_process_name(entry->d_name, process_name)) {
                return atoi(entry->d_name);
            }
        }
    }

    return 0;
}
