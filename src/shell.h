#pragma once
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#ifdef __linux__
#include <sys/wait.h>
#endif
#include <string>
#include <memory>

class Shell {
private:
    int to_shell[2];
    int from_shell[2];
    pid_t shell_pid;
    bool success;

#ifdef __linux__
    void setNonBlocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
#endif

    void writeCommand(const std::string& command);
    std::string readOutput();

public:
    Shell();
    ~Shell();
    std::string exec(std::string cmd);

};

extern std::unique_ptr<Shell> shell;
