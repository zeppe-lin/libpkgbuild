#include <pkgbuild/backends/curl.hpp>
#include <pkgbuild/backends/fakeroot.hpp>
#include <pkgbuild/backends/libarchive.hpp>
#include <pkgbuild/backends/openssl.hpp>
#include <pkgbuild/backends/pkgfile.hpp>
#include <pkgbuild/backends/posix.hpp>
#include <pkgbuild/engine.hpp>
#include <pkgbuild/error.hpp>

#include <archive.h>
#include <archive_entry.h>

#include <cerrno>
#include <cstdint>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <grp.h>
#include <iostream>
#include <map>
#include <memory>
#include <pwd.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

struct ArchiveCloser {
    void operator()(archive* value) const noexcept
    {
        if (value)
            archive_read_free(value);
    }
};

struct EntryInfo {
    mode_t type{0};
    mode_t mode{0};
    std::int64_t uid{0};
    std::int64_t gid{0};
    std::int64_t size{0};
    std::string symlink;
    std::string hardlink;
    std::int64_t device_major{0};
    std::int64_t device_minor{0};
    std::string payload;
};

[[noreturn]] void fail(const std::string& message)
{
    throw std::runtime_error(message);
}

void require(bool condition, const std::string& message)
{
    if (!condition)
        fail(message);
}

std::filesystem::path temporary_directory()
{
    std::string pattern = "/tmp/libpkgbuild-fakeroot.XXXXXX";
    std::vector<char> storage(pattern.begin(), pattern.end());
    storage.push_back('\0');
    char* created = mkdtemp(storage.data());
    if (!created)
        fail("cannot create temporary directory");
    return created;
}

pkgbuild::BuildIdentity nobody_identity()
{
    std::vector<char> buffer(16384);
    struct passwd record {};
    struct passwd* result = nullptr;
    if (getpwnam_r("nobody", &record, buffer.data(), buffer.size(), &result) != 0 ||
        result == nullptr)
        fail("cannot resolve nobody build identity");

    int count = 0;
    (void)getgrouplist(record.pw_name, record.pw_gid, nullptr, &count);
    std::vector<gid_t> groups(static_cast<std::size_t>(count));
    if (count != 0 &&
        getgrouplist(record.pw_name, record.pw_gid, groups.data(), &count) < 0)
        fail("cannot resolve nobody supplementary groups");
    groups.resize(static_cast<std::size_t>(count));

    return {
        record.pw_uid,
        record.pw_gid,
        std::move(groups),
        record.pw_dir ? record.pw_dir : "/",
        record.pw_name,
    };
}

std::map<std::string, EntryInfo>
read_archive(const std::filesystem::path& path)
{
    std::unique_ptr<archive, ArchiveCloser> input(archive_read_new());
    if (!input)
        fail("cannot allocate archive reader");
    archive_read_support_filter_all(input.get());
    archive_read_support_format_all(input.get());
    if (archive_read_open_filename(input.get(), path.c_str(), 64 * 1024) !=
        ARCHIVE_OK)
        fail("cannot open generated package archive");

    std::map<std::string, EntryInfo> result;
    archive_entry* entry = nullptr;
    while (archive_read_next_header(input.get(), &entry) == ARCHIVE_OK) {
        EntryInfo info;
        info.type = archive_entry_filetype(entry);
        info.mode = archive_entry_perm(entry);
        info.uid = archive_entry_uid(entry);
        info.gid = archive_entry_gid(entry);
        info.size = archive_entry_size(entry);
        if (const char* value = archive_entry_symlink(entry))
            info.symlink = value;
        if (const char* value = archive_entry_hardlink(entry))
            info.hardlink = value;
        info.device_major = archive_entry_rdevmajor(entry);
        info.device_minor = archive_entry_rdevminor(entry);

        char buffer[4096];
        for (;;) {
            const la_ssize_t count = archive_read_data(input.get(), buffer,
                                                       sizeof(buffer));
            if (count > 0) {
                info.payload.append(buffer, static_cast<std::size_t>(count));
                continue;
            }
            if (count == 0)
                break;
            fail("cannot read generated package payload");
        }
        result.emplace(archive_entry_pathname(entry), std::move(info));
    }
    return result;
}

void wait_until_dead(pid_t process)
{
    for (int attempt = 0; attempt != 50; ++attempt) {
        if (kill(process, 0) != 0 && errno == ESRCH)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    fail("recipe descendant survived process completion");
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 4) {
        std::cerr << "usage: fakeroot-metadata-test HELPER SCANNER FAKEROOT\n";
        return 2;
    }

    std::filesystem::path root;
    try {
        root = temporary_directory();
        (void)chmod(root.c_str(), 0755);
        const auto recipe = root / "metadata-fixture";
        std::filesystem::create_directories(recipe);
        std::filesystem::create_directories(root / "work");
        std::filesystem::create_directories(root / "packages");
        (void)chmod(recipe.c_str(), 0755);

        std::ofstream pkgfile(recipe / "Pkgfile");
        pkgfile <<
            "name=metadata-fixture\n"
            "version=1\n"
            "release=1\n"
            "source=\n"
            "build() {\n"
            "  mkdir -p \"$PKG/usr/lib/meta\" \"$PKG/dev\" \"$PKG/run\"\n"
            "  printf 'payload\\n' > \"$PKG/usr/lib/meta/payload\"\n"
            "  chmod 4750 \"$PKG/usr/lib/meta/payload\"\n"
            "  chown 123:456 \"$PKG/usr/lib/meta/payload\"\n"
            "  ln \"$PKG/usr/lib/meta/payload\" \"$PKG/usr/lib/meta/payload-link\"\n"
            "  ln -s payload \"$PKG/usr/lib/meta/current\"\n"
            "  mkfifo \"$PKG/run/channel\"\n"
            "  chmod 0620 \"$PKG/run/channel\"\n"
            "  chown 44:55 \"$PKG/run/channel\"\n"
            "  mknod \"$PKG/dev/nullish\" c 1 3\n"
            "  chmod 0600 \"$PKG/dev/nullish\"\n"
            "  chown 9:10 \"$PKG/dev/nullish\"\n"
            "  sleep 60 &\n"
            "  echo $! > \"$PKG/background.pid\"\n"
            "}\n";
        pkgfile.close();
        (void)chmod((recipe / "Pkgfile").c_str(), 0644);

        pkgbuild::ExecutionPolicy execution;
        execution.environment = {{"PATH", "/usr/bin:/bin"}, {"LANG", "C"}};
        if (geteuid() == 0)
            execution.identity = nobody_identity();

        pkgbuild::PosixProcessExecutor processes;
        pkgbuild::PkgfileDefinitionLoader definitions(argv[1], processes);
        pkgbuild::CurlDownloader downloader;
        pkgbuild::OpenSslSourceVerifier verifier;
        pkgbuild::LibarchiveBackend archives;
        pkgbuild::FakerootPkgfileRecipeRunner recipes(
            argv[3], argv[1], argv[2], processes);
        pkgbuild::NullPackageTransformer transformer;
        pkgbuild::Engine engine({definitions, downloader, verifier,
                                 archives, recipes, transformer, archives});
        pkgbuild::NullEventSink events;

        const auto receipt = engine.build(
            pkgbuild::BuildRequest{
                pkgbuild::DefinitionRequest{
                    pkgbuild::BuildPaths{
                        recipe,
                        recipe,
                        root / "packages",
                        root / "work",
                    },
                    std::nullopt,
                    {},
                    execution,
                },
                false,
                true,
                {},
                {},
            },
            events);

        require(receipt.work_directory.has_value(),
                "retained workspace was not reported");
        const auto entries = read_archive(receipt.package);

        const auto& payload = entries.at("usr/lib/meta/payload");
        require(payload.type == AE_IFREG, "payload is not regular");
        require(payload.mode == 04750, "virtual payload mode was lost");
        require(payload.uid == 123 && payload.gid == 456,
                "virtual payload ownership was lost");
        require(payload.payload == "payload\n", "payload bytes changed");

        const auto& hardlink = entries.at("usr/lib/meta/payload-link");
        require(hardlink.hardlink == "usr/lib/meta/payload",
                "hardlink relationship was lost");
        require(hardlink.payload.empty(), "hardlink duplicated payload bytes");

        const auto& symlink = entries.at("usr/lib/meta/current");
        require(symlink.type == AE_IFLNK && symlink.symlink == "payload",
                "symbolic link metadata was lost");

        const auto& fifo = entries.at("run/channel");
        require(fifo.type == AE_IFIFO && fifo.mode == 0620,
                "FIFO type or mode was lost");
        require(fifo.uid == 44 && fifo.gid == 55,
                "FIFO virtual ownership was lost");

        const auto& device = entries.at("dev/nullish");
        require(device.type == AE_IFCHR,
                "character-device type was lost");
        require(device.device_major == 1 && device.device_minor == 3,
                "character-device number was lost");
        require(device.uid == 9 && device.gid == 10,
                "character-device ownership was lost");

        const auto staged_payload = *receipt.work_directory /
                                    "pkg/usr/lib/meta/payload";
        struct stat host {};
        require(lstat(staged_payload.c_str(), &host) == 0,
                "cannot inspect retained staged payload");
        require(host.st_uid != 123 || host.st_gid != 456,
                "fakeroot changed real payload ownership");

        const pid_t background = static_cast<pid_t>(std::stol(
            entries.at("background.pid").payload));
        wait_until_dead(background);

        std::filesystem::remove_all(*receipt.work_directory);
        std::filesystem::remove_all(root);
        std::cout << "fakeroot metadata: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        if (!root.empty())
            std::filesystem::remove_all(root);
        std::cerr << "fakeroot metadata: " << error.what() << '\n';
        return 1;
    }
}
