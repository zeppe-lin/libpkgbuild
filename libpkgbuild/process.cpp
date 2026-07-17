#include "process.hpp"

#include <pkgbuild/backends/posix.hpp>
#include <pkgbuild/error.hpp>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace pkgbuild {
namespace {

enum class ChildStage : int {
    process_group = 1,
    working_directory,
    supplementary_groups,
    group_identity,
    user_identity,
    standard_output,
    execute,
};

struct ChildFailure {
    ChildStage stage;
    int error;
};

const char* stage_name(ChildStage stage)
{
    switch (stage) {
    case ChildStage::process_group: return "creating process group";
    case ChildStage::working_directory: return "changing working directory";
    case ChildStage::supplementary_groups: return "setting supplementary groups";
    case ChildStage::group_identity: return "setting group identity";
    case ChildStage::user_identity: return "setting user identity";
    case ChildStage::standard_output: return "redirecting standard output";
    case ChildStage::execute: return "executing child process";
    }
    return "preparing child process";
}

[[noreturn]] void child_fail(int fd, ChildStage stage)
{
    const ChildFailure failure{stage, errno};
    const char* bytes = reinterpret_cast<const char*>(&failure);
    std::size_t left = sizeof(failure);
    while (left != 0) {
        const ssize_t written = write(fd, bytes, left);
        if (written > 0) {
            bytes += written;
            left -= static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;
        break;
    }
    _exit(126);
}

void close_fd(int fd) noexcept
{
    if (fd >= 0)
        (void)close(fd);
}

void create_pipe(int pipefd[2], bool close_on_exec)
{
#ifdef O_CLOEXEC
    if (close_on_exec && pipe2(pipefd, O_CLOEXEC) == 0)
        return;
    if (close_on_exec && errno != ENOSYS)
        throw Error(ErrorCode::process_failed,
                    "pipe2 failed: " + std::string(std::strerror(errno)));
#endif
    if (pipe(pipefd) != 0)
        throw Error(ErrorCode::process_failed,
                    "pipe failed: " + std::string(std::strerror(errno)));
    if (close_on_exec) {
        if (fcntl(pipefd[0], F_SETFD, FD_CLOEXEC) != 0 ||
            fcntl(pipefd[1], F_SETFD, FD_CLOEXEC) != 0) {
            const int saved = errno;
            close_fd(pipefd[0]);
            close_fd(pipefd[1]);
            throw Error(ErrorCode::process_failed,
                        "fcntl failed: " + std::string(std::strerror(saved)));
        }
    }
}

std::vector<char*> make_argv(const ProcessRequest& request,
                             std::vector<std::string>& storage)
{
    storage.clear();
    storage.reserve(request.arguments.size() + 1);
    storage.push_back(request.program.string());
    storage.insert(storage.end(), request.arguments.begin(),
                   request.arguments.end());

    std::vector<char*> result;
    result.reserve(storage.size() + 1);
    for (auto& value : storage)
        result.push_back(value.data());
    result.push_back(nullptr);
    return result;
}

std::vector<char*> make_envp(const ProcessRequest& request,
                             std::vector<std::string>& storage)
{
    storage.clear();
    storage.reserve(request.environment.size());
    for (const auto& [name, value] : request.environment)
        storage.push_back(name + "=" + value);

    std::vector<char*> result;
    result.reserve(storage.size() + 1);
    for (auto& value : storage)
        result.push_back(value.data());
    result.push_back(nullptr);
    return result;
}

int wait_for(pid_t child, int& signal)
{
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR)
            continue;
        throw Error(ErrorCode::process_failed,
                    "waitpid failed: " + std::string(std::strerror(errno)));
    }

    signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (signal != 0)
        return 128 + signal;
    return 1;
}

std::string read_all(int fd)
{
    std::string output;
    char buffer[8192];
    for (;;) {
        const ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count > 0) {
            output.append(buffer, static_cast<std::size_t>(count));
            continue;
        }
        if (count == 0)
            return output;
        if (errno == EINTR)
            continue;
        throw Error(ErrorCode::process_failed,
                    "reading child output failed: " +
                        std::string(std::strerror(errno)));
    }
}

std::optional<ChildFailure> read_child_failure(int fd)
{
    ChildFailure failure{};
    char* bytes = reinterpret_cast<char*>(&failure);
    std::size_t have = 0;
    while (have != sizeof(failure)) {
        const ssize_t count = read(fd, bytes + have, sizeof(failure) - have);
        if (count > 0) {
            have += static_cast<std::size_t>(count);
            continue;
        }
        if (count == 0)
            return have == 0 ? std::nullopt
                             : std::optional<ChildFailure>(failure);
        if (errno == EINTR)
            continue;
        throw Error(ErrorCode::process_failed,
                    "reading child setup status failed: " +
                        std::string(std::strerror(errno)));
    }
    return failure;
}

} // namespace

void validate_execution_policy(const ExecutionPolicy& policy)
{
    if (!policy.identity) {
        if (geteuid() == 0)
            throw Error(ErrorCode::invalid_configuration,
                        "root caller requires an explicit non-root build identity");
        return;
    }

    const auto& identity = *policy.identity;
    if (identity.uid == 0 || identity.gid == 0)
        throw Error(ErrorCode::invalid_configuration,
                    "build identity must not use the root user or group");
    for (const gid_t group : identity.supplementary_groups) {
        if (group == 0)
            throw Error(ErrorCode::invalid_configuration,
                        "build identity must not retain the root group");
    }
    if (identity.user.empty())
        throw Error(ErrorCode::invalid_configuration,
                    "build identity requires a user name");
    if (identity.home.empty() || !identity.home.is_absolute())
        throw Error(ErrorCode::invalid_configuration,
                    "build identity requires an absolute home directory");

    if (geteuid() != 0 &&
        (identity.uid != geteuid() || identity.gid != getegid()))
        throw Error(ErrorCode::invalid_configuration,
                    "unprivileged caller cannot select another build identity");
}

ProcessResult PosixProcessExecutor::execute(const ProcessRequest& request) const
{
    if (request.program.empty() || !request.program.is_absolute())
        throw Error(ErrorCode::invalid_configuration,
                    "process program must be an absolute path");

    validate_execution_policy(ExecutionPolicy{
        request.identity,
        request.environment,
        request.file_creation_mask,
    });

    int output_pipe[2] = {-1, -1};
    int failure_pipe[2] = {-1, -1};
    if (request.capture_stdout)
        create_pipe(output_pipe, false);
    try {
        create_pipe(failure_pipe, true);
    } catch (...) {
        close_fd(output_pipe[0]);
        close_fd(output_pipe[1]);
        throw;
    }

    const pid_t child = fork();
    if (child < 0) {
        const int saved = errno;
        close_fd(output_pipe[0]);
        close_fd(output_pipe[1]);
        close_fd(failure_pipe[0]);
        close_fd(failure_pipe[1]);
        throw Error(ErrorCode::process_failed,
                    "fork failed: " + std::string(std::strerror(saved)));
    }

    if (child == 0) {
        close_fd(failure_pipe[0]);
        if (request.capture_stdout) {
            close_fd(output_pipe[0]);
            if (dup2(output_pipe[1], STDOUT_FILENO) < 0)
                child_fail(failure_pipe[1], ChildStage::standard_output);
            close_fd(output_pipe[1]);
        }

        if (request.create_process_group && setpgid(0, 0) != 0)
            child_fail(failure_pipe[1], ChildStage::process_group);
        if (!request.working_directory.empty() &&
            chdir(request.working_directory.c_str()) != 0)
            child_fail(failure_pipe[1], ChildStage::working_directory);

        (void)umask(request.file_creation_mask);

        if (request.identity) {
            const auto& identity = *request.identity;
            if (geteuid() == 0) {
                if (setgroups(identity.supplementary_groups.size(),
                              identity.supplementary_groups.empty() ? nullptr :
                                  identity.supplementary_groups.data()) != 0)
                    child_fail(failure_pipe[1],
                               ChildStage::supplementary_groups);
                if (setgid(identity.gid) != 0)
                    child_fail(failure_pipe[1], ChildStage::group_identity);
                if (setuid(identity.uid) != 0)
                    child_fail(failure_pipe[1], ChildStage::user_identity);
            } else if (identity.uid != geteuid() || identity.gid != getegid()) {
                errno = EPERM;
                child_fail(failure_pipe[1], ChildStage::user_identity);
            }
        }

        std::vector<std::string> argument_storage;
        std::vector<std::string> environment_storage;
        auto argv = make_argv(request, argument_storage);
        auto envp = make_envp(request, environment_storage);
        execve(request.program.c_str(), argv.data(), envp.data());
        child_fail(failure_pipe[1], ChildStage::execute);
    }

    close_fd(failure_pipe[1]);
    close_fd(output_pipe[1]);

    std::optional<ChildFailure> failure;
    try {
        failure = read_child_failure(failure_pipe[0]);
    } catch (...) {
        close_fd(failure_pipe[0]);
        close_fd(output_pipe[0]);
        int ignored = 0;
        (void)wait_for(child, ignored);
        throw;
    }
    close_fd(failure_pipe[0]);

    std::string output;
    if (request.capture_stdout) {
        try {
            output = read_all(output_pipe[0]);
        } catch (...) {
            close_fd(output_pipe[0]);
            int ignored = 0;
            (void)wait_for(child, ignored);
            throw;
        }
        close_fd(output_pipe[0]);
    }

    int termination_signal = 0;
    const int status = wait_for(child, termination_signal);
    if (failure) {
        throw Error(ErrorCode::process_failed,
                    std::string(stage_name(failure->stage)) + " failed: " +
                        std::strerror(failure->error));
    }

    return ProcessResult{status, termination_signal, std::move(output)};
}

namespace detail {

std::vector<std::string> split_nul(const std::string& data)
{
    std::vector<std::string> fields;
    std::size_t begin = 0;
    while (begin < data.size()) {
        const auto end = data.find('\0', begin);
        if (end == std::string::npos)
            throw Error(ErrorCode::invalid_definition,
                        "definition worker returned an unterminated field");
        fields.emplace_back(data.substr(begin, end - begin));
        begin = end + 1;
    }
    return fields;
}

} // namespace detail
} // namespace pkgbuild
