#include "shell.h"
#include <thread>
#include <iostream>
#include <sys/wait.h>
#include <spdlog/spdlog.h>
#include "string_utils.h"

std::string Shell::readOutput() {
    std::string output;
    char buffer[256];
    ssize_t bytesRead;
    bool dataAvailable = false;

    // Wait for up to 500 milliseconds for output to become available
    for (int i = 0; i < 10; ++i) {
        bytesRead = read(from_shell[0], buffer, sizeof(buffer) - 1);
        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            output += buffer;
            dataAvailable = true;
            break; // Break as soon as we get some data
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    // If we detected data, keep reading until no more is available
    while (dataAvailable) {
        bytesRead = read(from_shell[0], buffer, sizeof(buffer) - 1);
        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            output += buffer;
        } else {
            break; // No more data available
        }
    }
    
    trim(output);
    SPDLOG_DEBUG("Shell: recieved output: {}", output);
    return output;
}

Shell::Shell() {
    static bool failed;
    if (pipe(to_shell) == -1) {
        SPDLOG_ERROR("Failed to create to_shell pipe: {}", strerror(errno));
        failed = true;
    }

    if (pipe(from_shell) == -1) {
        SPDLOG_ERROR("Failed to create from_shell pipe: {}", strerror(errno));
        failed = true;
    }

    // if either pipe fails, there's no point in continuing.
    if (failed){
        SPDLOG_ERROR("Shell has failed, will not be able to use exec");
        return;
    }

    shell_pid = fork();

    if (shell_pid == 0) { // Child process
        close(to_shell[1]);
        close(from_shell[0]);

        dup2(to_shell[0], STDIN_FILENO);
        dup2(from_shell[1], STDOUT_FILENO);
        dup2(from_shell[1], STDERR_FILENO);
        execl("/bin/sh", "sh", nullptr);
        exit(1); // Exit if execl fails
    } else {
        close(to_shell[0]);
        close(from_shell[1]);

        // Set the read end of the from_shell pipe to non-blocking
        setNonBlocking(from_shell[0]);
    }
    success = true;
}

std::string Shell::exec(std::string cmd) {
    if (!success)
        return "";

    writeCommand(cmd);
    return readOutput();
}

void Shell::writeCommand(std::string command) {
    if (write(to_shell[1], command.c_str(), command.length()) == -1)
        SPDLOG_ERROR("Failed to write to shell");
    
    trim(command);
    SPDLOG_DEBUG("Shell: wrote command: {}", command);
}

Shell::~Shell() {
    if (write(to_shell[1], "exit\n", 5) == -1)
        SPDLOG_ERROR("Failed exit shell");
    close(to_shell[1]);
    close(from_shell[0]);
    waitpid(shell_pid, nullptr, 0);
}