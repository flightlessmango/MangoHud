#pragma once
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <sys/wait.h>
#include <string>
#include <memory>

class Shell {
private:
    int to_shell[2];
    int from_shell[2];
    pid_t shell_pid;

    void setNonBlocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    void writeCommand(const std::string& command) {
        write(to_shell[1], command.c_str(), command.length());
    }

    std::string readOutput();

public:
    Shell();
    ~Shell();
    std::string exec(std::string cmd);

};

extern std::unique_ptr<Shell> shell;
