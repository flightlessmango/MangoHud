#include "exec.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

#include "string_utils.h"

extern char **environ;

Exec::Exec()
{
    thread = std::thread(&Exec::run, this);
}

Exec::~Exec()
{
    stop.store(true);
    if (thread.joinable())
        thread.join();

    std::lock_guard lock(m);
    entries.clear();
}

std::pair<bool, std::string> Exec::get(const std::string& command)
{
    std::shared_ptr<Entry> entry;

    {
        std::lock_guard lock(m);
        auto& slot = entries[command];
        if (!slot)
            slot = std::make_shared<Entry>();
        entry = slot;
    }

    std::lock_guard lock(entry->m);
    entry->last_request = std::chrono::steady_clock::now();
    return {entry->valid, entry->cached_output};
}

Exec::Job::Job(pid_t pid_, unique_fd stdout_fd_, unique_fd stderr_fd_,
               std::chrono::steady_clock::time_point started_)
    : pid(pid_),
      stdout_fd(std::move(stdout_fd_)),
      stderr_fd(std::move(stderr_fd_)),
      started(started_)
{}

Exec::Job::~Job()
{
    if (pid > 0) {
        kill(-pid, SIGKILL);
        while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
    }
}

void Exec::run()
{
    while (!stop.load()) {
        tick();
        std::this_thread::sleep_for(interval);
    }
}

void Exec::tick()
{
    auto now = std::chrono::steady_clock::now();
    EntryList running_entries;

    {
        std::lock_guard lock(m);
        running_entries.reserve(entries.size());
        for (auto& [command, entry] : entries)
            running_entries.emplace_back(command, entry);
    }

    update_running_jobs(running_entries, now);

    EntryList active_entries;
    {
        std::lock_guard lock(m);
        remove_stale_entries(now);
        active_entries.reserve(entries.size());
        for (auto& [command, entry] : entries)
            active_entries.emplace_back(command, entry);
    }

    auto spawn_started = std::chrono::steady_clock::now();
    for (auto& [command, entry] : active_entries)
        spawn(command, entry.get());

    auto spawn_elapsed = std::chrono::steady_clock::now() - spawn_started;
    if (spawn_elapsed > interval) {
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(spawn_elapsed).count();
        auto interval_ms = std::chrono::duration_cast<std::chrono::milliseconds>(interval).count();
        SPDLOG_ERROR("exec spawning took {} ms, longer than interval {} ms",
                     elapsed_ms, interval_ms);
    }
}

void Exec::remove_stale_entries(std::chrono::steady_clock::time_point now)
{
    for (auto it = entries.begin(); it != entries.end();) {
        auto entry = it->second;
        bool stale = false;
        {
            std::lock_guard lock(entry->m);
            stale = !entry->running() && now - entry->last_request > stale_after;
        }

        if (stale)
            it = entries.erase(it);
        else
            ++it;
    }
}

void Exec::update_running_jobs(EntryList& active_entries,
                               std::chrono::steady_clock::time_point now)
{
    for (auto& [command, entry] : active_entries) {
        std::lock_guard lock(entry->m);
        if (!entry->job)
            continue;

        drain_job(*entry->job);

        int status = 0;
        pid_t ret = waitpid(entry->job->pid, &status, WNOHANG);
        if (ret == entry->job->pid) {
            drain_job(*entry->job);
            auto job = std::move(entry->job);
            job->pid = -1;
            finish_job(command, entry.get(), std::move(job), status, false);
            continue;
        }
        if (ret < 0) {
            if (errno == EINTR)
                continue;

            SPDLOG_ERROR("exec '{}' waitpid failed: {}", command, strerror(errno));
            auto job = std::move(entry->job);
            job->pid = -1;
            continue;
        }

        if (now - entry->job->started < timeout)
            continue;

        kill(-entry->job->pid, SIGKILL);
        while (waitpid(entry->job->pid, &status, 0) < 0 && errno == EINTR) {}
        drain_job(*entry->job);

        auto job = std::move(entry->job);
        job->pid = -1;
        finish_job(command, entry.get(), std::move(job), status, true);
    }
}

void Exec::drain_job(Job& job)
{
    drain_fd(job.stdout_fd, job.stdout_output, stdout_limit);
    drain_fd(job.stderr_fd, job.stderr_output, stderr_limit);
}

void Exec::drain_fd(unique_fd& fd, std::string& output, size_t limit)
{
    if (!fd)
        return;

    char buffer[1024];
    for (;;) {
        ssize_t ret = read(fd.get(), buffer, sizeof(buffer));
        if (ret > 0) {
            if (output.size() < limit) {
                size_t remaining = limit - output.size();
                output.append(buffer, std::min(static_cast<size_t>(ret), remaining));
            }
            continue;
        }

        if (ret == 0) {
            fd.reset();
            return;
        }

        if (errno == EINTR)
            continue;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;

        fd.reset();
        return;
    }
}

void Exec::finish_job(const std::string& command, Entry* entry, std::unique_ptr<Job> job,
                      int status, bool timed_out)
{
    if (!job)
        return;

    std::string stdout_text = trim_copy(job->stdout_output);
    std::string stderr_text = trim_copy(job->stderr_output);

    if (!timed_out && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        entry->cached_output = std::move(stdout_text);
        entry->valid = true;

        if (entry->cached_output.empty() && !stderr_text.empty() &&
            entry->last_stderr_log != stderr_text) {
            SPDLOG_DEBUG("exec '{}' wrote to stderr but stdout was empty: {}",
                         command, stderr_text);
            entry->last_stderr_log = stderr_text;
        }
    } else if (timed_out) {
        if (!stderr_text.empty())
            SPDLOG_ERROR("exec '{}' timed out: {}", command, stderr_text);
        else
            SPDLOG_ERROR("exec '{}' timed out", command);
    } else if (WIFEXITED(status)) {
        if (!stderr_text.empty())
            SPDLOG_DEBUG("exec '{}' exited with status {}: {}",
                         command, WEXITSTATUS(status), stderr_text);
        else
            SPDLOG_DEBUG("exec '{}' exited with status {}",
                         command, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        if (!stderr_text.empty())
            SPDLOG_DEBUG("exec '{}' killed by signal {}: {}",
                         command, WTERMSIG(status), stderr_text);
        else
            SPDLOG_DEBUG("exec '{}' killed by signal {}",
                         command, WTERMSIG(status));
    }
}

bool Exec::spawn(const std::string& command, Entry* entry)
{
    std::lock_guard lock(entry->m);
    if (entry->running())
        return false;

    int stdout_pipe[2];
    if (pipe2(stdout_pipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        log_spawn_error(command, *entry, strerror(errno));
        return false;
    }
    unique_fd stdout_read = unique_fd::adopt(stdout_pipe[0]);
    unique_fd stdout_write = unique_fd::adopt(stdout_pipe[1]);

    int stderr_pipe[2];
    if (pipe2(stderr_pipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        log_spawn_error(command, *entry, strerror(errno));
        return false;
    }
    unique_fd stderr_read = unique_fd::adopt(stderr_pipe[0]);
    unique_fd stderr_write = unique_fd::adopt(stderr_pipe[1]);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, stdout_write.get(), STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stderr_write.get(), STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, stdout_read.get());
    posix_spawn_file_actions_addclose(&actions, stderr_read.get());

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);

    short flags = POSIX_SPAWN_SETPGROUP;
    posix_spawnattr_setflags(&attr, flags);
    posix_spawnattr_setpgroup(&attr, 0);

    char* argv[] = {
        const_cast<char*>("/bin/sh"),
        const_cast<char*>("-c"),
        const_cast<char*>(command.c_str()),
        nullptr,
    };

    pid_t pid = -1;
    int rc = posix_spawn(&pid, "/bin/sh", &actions, &attr, argv, environ);

    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);

    if (rc != 0) {
        log_spawn_error(command, *entry, strerror(rc));
        return false;
    }

    entry->job = std::make_unique<Job>(
        pid,
        std::move(stdout_read),
        std::move(stderr_read),
        std::chrono::steady_clock::now());
    entry->last_spawn_error.clear();
    return true;
}

void Exec::log_spawn_error(const std::string& command, Entry& entry, const char* error)
{
    std::string message = error ? error : "unknown error";
    if (entry.last_spawn_error == message)
        return;

    SPDLOG_DEBUG("exec '{}' spawn failed: {}", command, message);
    entry.last_spawn_error = std::move(message);
}
