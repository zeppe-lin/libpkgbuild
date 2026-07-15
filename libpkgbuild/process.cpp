#include "process.hpp"

#include <pkgbuild/error.hpp>

#include <cerrno>
#include <cstring>
#include <memory>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace pkgbuild::detail {
namespace {

std::vector<char*> make_argv(const std::filesystem::path& program,
                             const std::vector<std::string>& arguments,
                             std::vector<std::string>& storage)
{
    storage.clear();
    storage.reserve(arguments.size() + 1);
    storage.push_back(program.string());
    storage.insert(storage.end(), arguments.begin(), arguments.end());

    std::vector<char*> argv;
    argv.reserve(storage.size() + 1);
    for (auto& item : storage)
        argv.push_back(item.data());
    argv.push_back(nullptr);
    return argv;
}

int wait_for(pid_t child)
{
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR)
            continue;
        throw Error(ErrorCode::process_failed,
                    "waitpid failed: " + std::string(std::strerror(errno)));
    }

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 1;
}

} // namespace

ProcessResult run_capture(const std::filesystem::path& program,
                          const std::vector<std::string>& arguments)
{
    int pipefd[2];
    if (pipe(pipefd) < 0)
        throw Error(ErrorCode::process_failed,
                    "pipe failed: " + std::string(std::strerror(errno)));

    const pid_t child = fork();
    if (child < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        throw Error(ErrorCode::process_failed,
                    "fork failed: " + std::string(std::strerror(errno)));
    }

    if (child == 0) {
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0)
            _exit(126);
        close(pipefd[1]);

        std::vector<std::string> storage;
        auto argv = make_argv(program, arguments, storage);
        execv(program.c_str(), argv.data());
        _exit(errno == ENOENT ? 127 : 126);
    }

    close(pipefd[1]);
    std::string output;
    char buffer[8192];
    for (;;) {
        const ssize_t n = read(pipefd[0], buffer, sizeof(buffer));
        if (n > 0) {
            output.append(buffer, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0)
            break;
        if (errno == EINTR)
            continue;
        close(pipefd[0]);
        (void)wait_for(child);
        throw Error(ErrorCode::process_failed,
                    "read from child failed: " +
                        std::string(std::strerror(errno)));
    }
    close(pipefd[0]);

    return ProcessResult{wait_for(child), std::move(output)};
}

int run_inherit(const std::filesystem::path& program,
                const std::vector<std::string>& arguments)
{
    const pid_t child = fork();
    if (child < 0)
        throw Error(ErrorCode::process_failed,
                    "fork failed: " + std::string(std::strerror(errno)));

    if (child == 0) {
        std::vector<std::string> storage;
        auto argv = make_argv(program, arguments, storage);
        execv(program.c_str(), argv.data());
        _exit(errno == ENOENT ? 127 : 126);
    }

    return wait_for(child);
}

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

} // namespace pkgbuild::detail
