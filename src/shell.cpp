#include "shell.h"
#include <thread>
#include <iostream>
#include <sys/wait.h>
#include <spdlog/spdlog.h>
#include "string_utils.h"
#include <array>

std::string Shell::readOutput() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::array<char, 128> buffer;
    std::string result;
    ssize_t count;
    while ((count = ::read(from_shell[0], buffer.data(), buffer.size())) > 0) {
        result.append(buffer.data(), count);
    }

    // Split the result into lines and return the last line
    std::istringstream stream(result);
    std::string line;
    std::string last_line;
    while (std::getline(stream, line)) {
        last_line = line;
    }

    SPDLOG_DEBUG("Shell: recieved output: {}", last_line);
    return last_line;
}

Shell::Shell() {
    if (stat("/run/pressure-vessel", &stat_buffer) == 0)
        runtime = true;

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
        execl("/bin/sh", "sh", "-c", "unset LD_PRELOAD; exec /bin/sh", nullptr);
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
    
    if (runtime)
        command = "steam-runtime-launch-client --alongside-steam --host -- " + command;
    
    trim(command);
    SPDLOG_DEBUG("Shell: wrote command: {}", command);
}

Shell::~Shell() {
    close(to_shell[1]);
    close(from_shell[0]);
    waitpid(shell_pid, nullptr, 0);
}
