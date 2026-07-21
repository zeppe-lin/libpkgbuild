#include <pkgbuild/backends/curl.hpp>
#include <pkgbuild/backends/fakeroot.hpp>
#include <pkgbuild/backends/libarchive.hpp>
#include <pkgbuild/backends/normalize.hpp>
#include <pkgbuild/backends/openssl.hpp>
#include <pkgbuild/backends/pkgfile.hpp>
#include <pkgbuild/backends/posix.hpp>
#include <pkgbuild/engine.hpp>

#include <archive.h>
#include <archive_entry.h>

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
    std::string hardlink;
    std::string symlink;
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
    std::string pattern = "/tmp/libpkgbuild-normalization.XXXXXX";
    std::vector<char> storage(pattern.begin(), pattern.end());
    storage.push_back('\0');
    char* created = mkdtemp(storage.data());
    if (!created)
        fail("cannot create temporary directory");
    (void)chmod(created, 0755);
    return created;
}

pkgbuild::BuildIdentity nobody_identity()
{
    std::vector<char> buffer(16384);
    struct passwd record {};
    struct passwd* result = nullptr;
    if (getpwnam_r("nobody", &record, buffer.data(), buffer.size(), &result) != 0 ||
        !result)
        fail("cannot resolve nobody build identity");

    int count = 0;
    (void)getgrouplist(record.pw_name, record.pw_gid, nullptr, &count);
    std::vector<gid_t> groups(static_cast<std::size_t>(count));
    if (count != 0 &&
        getgrouplist(record.pw_name, record.pw_gid, groups.data(), &count) < 0)
        fail("cannot resolve nobody supplementary groups");
    groups.resize(static_cast<std::size_t>(count));
    return {record.pw_uid, record.pw_gid, std::move(groups),
            record.pw_dir ? record.pw_dir : "/", record.pw_name};
}

std::map<std::string, EntryInfo> read_archive(const std::filesystem::path& path)
{
    std::unique_ptr<archive, ArchiveCloser> input(archive_read_new());
    if (!input)
        fail("cannot allocate archive reader");
    archive_read_support_filter_all(input.get());
    archive_read_support_format_all(input.get());
    if (archive_read_open_filename(input.get(), path.c_str(), 64 * 1024) !=
        ARCHIVE_OK)
        fail("cannot open generated package");

    std::map<std::string, EntryInfo> result;
    archive_entry* entry = nullptr;
    while (archive_read_next_header(input.get(), &entry) == ARCHIVE_OK) {
        EntryInfo info;
        if (const char* value = archive_entry_hardlink(entry))
            info.hardlink = value;
        if (const char* value = archive_entry_symlink(entry))
            info.symlink = value;
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

bool has_kind(const pkgbuild::LegacyBuildReceipt& receipt,
              pkgbuild::TransformationKind kind)
{
    for (const auto& transformation : receipt.transformations) {
        for (const auto& change : transformation.changes) {
            if (change.kind == kind)
                return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 5) {
        std::cerr << "usage: normalization-pipeline-test "
                     "HELPER SCANNER FAKEROOT STRIP\n";
        return 2;
    }

    std::filesystem::path root;
    try {
        root = temporary_directory();
        const auto recipe = root / "normalize";
        std::filesystem::create_directories(recipe);
        std::filesystem::create_directories(root / "packages");
        std::filesystem::create_directories(root / "work");
        (void)chmod(recipe.c_str(), 0755);

        std::ofstream pkgfile(recipe / "Pkgfile");
        pkgfile <<
            "name=normalize\n"
            "version=1\n"
            "release=1\n"
            "source=\n"
            "build() {\n"
            "  mkdir -p \"$PKG/usr/bin\" \"$PKG/usr/share/man/man1\"\n"
            "  cp /bin/true \"$PKG/usr/bin/normalize\"\n"
            "  ln \"$PKG/usr/bin/normalize\" \"$PKG/usr/bin/normalize-alias\"\n"
            "  printf 'normalize manual\\n' > \"$PKG/usr/share/man/man1/normalize.1\"\n"
            "  ln \"$PKG/usr/share/man/man1/normalize.1\" \"$PKG/usr/share/man/man1/normalize-alias.1\"\n"
            "  ln -s normalize.1 \"$PKG/usr/share/man/man1/normalize-link.1\"\n"
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
        pkgbuild::PackageTreeTransformer transformer(argv[4], processes);
        pkgbuild::Engine engine({definitions, downloader, verifier, archives,
                                 recipes, transformer, archives});
        pkgbuild::NullEventSink events;

        const auto receipt = engine.build(pkgbuild::BuildRequest{
            pkgbuild::DefinitionRequest{
                {recipe, recipe, root / "packages", root / "work"},
                std::nullopt,
                {},
                execution,
            },
            false,
            false,
            {},
            {},
            std::nullopt,
        }, events);

        require(receipt.transformations.size() == 1,
                "engine did not retain transformation receipt");
        require(has_kind(receipt, pkgbuild::TransformationKind::strip_binary),
                "engine did not record binary stripping");
        require(has_kind(receipt,
                         pkgbuild::TransformationKind::compress_manpage),
                "engine did not record man page compression");
        require(has_kind(receipt,
                         pkgbuild::TransformationKind::rewrite_manpage_symlink),
                "engine did not record man page symlink rewrite");

        const auto entries = read_archive(receipt.package);
        require(entries.count("usr/bin/normalize") == 1 &&
                    entries.count("usr/bin/normalize-alias") == 1,
                "stripped binaries are missing from archive");
        const bool binary_hardlink =
            !entries.at("usr/bin/normalize").hardlink.empty() ||
            !entries.at("usr/bin/normalize-alias").hardlink.empty();
        require(binary_hardlink, "binary hardlink was not preserved in archive");

        require(entries.count("usr/share/man/man1/normalize.1.gz") == 1 &&
                    entries.count("usr/share/man/man1/normalize-alias.1.gz") == 1,
                "compressed man pages are missing from archive");
        require(entries.at("usr/share/man/man1/normalize-link.1.gz").symlink ==
                    "normalize.1.gz",
                "compressed man page symlink is wrong");
        const auto& gzip_payload =
            entries.at("usr/share/man/man1/normalize.1.gz").payload.empty()
                ? entries.at("usr/share/man/man1/normalize-alias.1.gz").payload
                : entries.at("usr/share/man/man1/normalize.1.gz").payload;
        require(gzip_payload.size() >= 2 &&
                    static_cast<unsigned char>(gzip_payload[0]) == 0x1f &&
                    static_cast<unsigned char>(gzip_payload[1]) == 0x8b,
                "archive contains an invalid compressed man page");

        std::filesystem::remove_all(root);
        std::cout << "normalization pipeline: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        if (!root.empty())
            std::filesystem::remove_all(root);
        std::cerr << "normalization pipeline: " << error.what() << '\n';
        return 1;
    }
}
