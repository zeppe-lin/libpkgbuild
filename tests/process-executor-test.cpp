#include <pkgbuild/backends/posix.hpp>
#include <pkgbuild/error.hpp>

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <grp.h>
#include <iostream>
#include <pwd.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

[[noreturn]] void fail(const std::string& message)
{
    throw std::runtime_error(message);
}

void require(bool value, const std::string& message)
{
    if (!value)
        fail(message);
}

template<typename Function>
void require_error(pkgbuild::ErrorCode code, Function function)
{
    try {
        function();
    } catch (const pkgbuild::Error& error) {
        require(error.code() == code, "unexpected pkgbuild error code");
        return;
    }
    fail("expected pkgbuild error");
}

std::filesystem::path temporary_directory()
{
    std::string pattern = "/tmp/libpkgbuild-process.XXXXXX";
    std::vector<char> storage(pattern.begin(), pattern.end());
    storage.push_back('\0');
    char* result = mkdtemp(storage.data());
    if (!result)
        fail("mkdtemp failed");
    return result;
}

pkgbuild::BuildIdentity nobody_identity()
{
    std::vector<char> buffer(16384);
    struct passwd record {};
    struct passwd* result = nullptr;
    const int status = getpwnam_r("nobody", &record, buffer.data(),
                                  buffer.size(), &result);
    if (status != 0 || !result)
        fail("nobody user is required for root execution tests");

    int count = 0;
    (void)getgrouplist(record.pw_name, record.pw_gid, nullptr, &count);
    std::vector<gid_t> groups(static_cast<std::size_t>(count));
    if (count != 0 &&
        getgrouplist(record.pw_name, record.pw_gid, groups.data(), &count) < 0)
        fail("cannot read nobody supplementary groups");
    groups.resize(static_cast<std::size_t>(count));

    return pkgbuild::BuildIdentity{
        record.pw_uid,
        record.pw_gid,
        std::move(groups),
        record.pw_dir ? record.pw_dir : "/",
        record.pw_name,
    };
}

pkgbuild::ProcessRequest request_for(const std::filesystem::path& program,
                                     const std::filesystem::path& cwd)
{
    return pkgbuild::ProcessRequest{
        program,
        {},
        cwd,
        {{"PATH", "/usr/bin:/bin"}, {"LANG", "C"}},
        std::nullopt,
        0022,
        true,
        true,
    };
}

std::string strip_newline(std::string value)
{
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
        value.pop_back();
    return value;
}

} // namespace

int main()
{
    try {
        pkgbuild::PosixProcessExecutor executor;
        const auto directory = temporary_directory();

        auto identity_request = request_for("/usr/bin/id", directory);
        identity_request.arguments = {"-u"};

        if (geteuid() == 0) {
            require_error(pkgbuild::ErrorCode::invalid_configuration, [&] {
                (void)executor.execute(identity_request);
            });

            auto root = nobody_identity();
            root.uid = 0;
            identity_request.identity = root;
            require_error(pkgbuild::ErrorCode::invalid_configuration, [&] {
                (void)executor.execute(identity_request);
            });

            const auto nobody = nobody_identity();
            identity_request.identity = nobody;
            const auto result = executor.execute(identity_request);
            require(result.ok(), "id command failed after identity drop");
            require(strip_newline(result.stdout_data) ==
                        std::to_string(nobody.uid),
                    "child retained the wrong user identity");
        } else {
            const auto result = executor.execute(identity_request);
            require(result.ok(), "id command failed");
            require(strip_newline(result.stdout_data) ==
                        std::to_string(geteuid()),
                    "child changed the caller identity");
        }

        auto invalid_environment = request_for("/usr/bin/env", directory);
        invalid_environment.environment["BAD=NAME"] = "value";
        if (geteuid() == 0)
            invalid_environment.identity = nobody_identity();
        require_error(pkgbuild::ErrorCode::invalid_configuration, [&] {
            (void)executor.execute(invalid_environment);
        });

        auto invalid_mask = request_for("/usr/bin/env", directory);
        invalid_mask.file_creation_mask = 01000;
        if (geteuid() == 0)
            invalid_mask.identity = nobody_identity();
        require_error(pkgbuild::ErrorCode::invalid_configuration, [&] {
            (void)executor.execute(invalid_mask);
        });

        auto environment = request_for("/usr/bin/env", directory);
        environment.environment["LIBPKGBUILD_MARKER"] = "selected";
        if (geteuid() == 0)
            environment.identity = nobody_identity();
        setenv("LIBPKGBUILD_AMBIENT", "must-not-leak", 1);
        const auto env_result = executor.execute(environment);
        require(env_result.ok(), "env command failed");
        require(env_result.stdout_data.find("LIBPKGBUILD_MARKER=selected") !=
                    std::string::npos,
                "selected environment was not passed");
        require(env_result.stdout_data.find("LIBPKGBUILD_AMBIENT") ==
                    std::string::npos,
                "ambient environment leaked into the child");

        auto pwd = request_for("/bin/pwd", directory);
        if (geteuid() == 0)
            pwd.identity = nobody_identity();
        const auto pwd_result = executor.execute(pwd);
        require(pwd_result.ok(), "pwd command failed");
        require(std::filesystem::path(strip_newline(pwd_result.stdout_data)) ==
                    directory,
                "child used the wrong working directory");

        auto mask = request_for("/bin/sh", directory);
        mask.arguments = {"-c", "umask"};
        mask.file_creation_mask = 0027;
        if (geteuid() == 0)
            mask.identity = nobody_identity();
        const auto mask_result = executor.execute(mask);
        require(mask_result.ok(), "umask command failed");
        require(strip_newline(mask_result.stdout_data) == "0027",
                "child used the wrong umask");

        auto combined = request_for("/bin/sh", directory);
        combined.arguments = {"-c", "printf 'stdout\n'; printf 'stderr\n' >&2"};
        combined.merge_stderr = true;
        if (geteuid() == 0)
            combined.identity = nobody_identity();
        const auto combined_result = executor.execute(combined);
        require(combined_result.ok(), "combined output command failed");
        require(combined_result.stdout_data == "stdout\nstderr\n",
                "standard error was not merged into captured output");

        auto invalid_merge = request_for("/usr/bin/true", directory);
        invalid_merge.capture_stdout = false;
        invalid_merge.merge_stderr = true;
        if (geteuid() == 0)
            invalid_merge.identity = nobody_identity();
        require_error(pkgbuild::ErrorCode::invalid_configuration, [&] {
            (void)executor.execute(invalid_merge);
        });

        auto signal = request_for("/bin/sh", directory);
        signal.arguments = {"-c", "kill -TERM $$"};
        if (geteuid() == 0)
            signal.identity = nobody_identity();
        const auto signal_result = executor.execute(signal);
        require(signal_result.termination_signal == 15,
                "signal termination was not preserved");
        require(signal_result.exit_status == 143,
                "signal exit status was not normalized");

        std::filesystem::remove_all(directory);
        std::cout << "process executor: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "process executor: " << error.what() << '\n';
        return 1;
    }
}
