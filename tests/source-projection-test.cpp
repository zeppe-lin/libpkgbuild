#include <pkgbuild/definition.hpp>
#include <pkgbuild/error.hpp>

#include <libpkgsource/pkgfile_backend.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
namespace fs = std::filesystem;

class TemporaryDirectory final {
public:
    TemporaryDirectory()
    {
        std::string pattern =
            (fs::temp_directory_path() / "pkgbuild-projection.XXXXXX")
                .string();
        std::vector<char> storage(pattern.begin(), pattern.end());
        storage.push_back('\0');
        char* created = mkdtemp(storage.data());
        if (created == nullptr)
            throw std::runtime_error("cannot create temporary directory");
        path_ = created;
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

void write_file(const fs::path& path, const std::string& contents)
{
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output)
        throw std::runtime_error("cannot create " + path.string());
    output << contents;
    if (!output)
        throw std::runtime_error("cannot write " + path.string());
}

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template<class Function>
void expect_pkgbuild_error(pkgbuild::ErrorCode code, Function&& function)
{
    try {
        function();
    } catch (const pkgbuild::Error& error) {
        if (error.code() != code)
            throw std::runtime_error("wrong pkgbuild error code");
        return;
    }
    throw std::runtime_error("expected pkgbuild error");
}

} // namespace

int main()
{
    TemporaryDirectory temporary;
    const auto origin = temporary.path() / "sample";
    fs::create_directories(origin);

    write_file(origin / "Pkgfile",
               "name=sample\n"
               "version=1.2\n"
               "release=3\n"
               "source=\"renamed.tar.xz::https://example.invalid/"
               "upstream.tar.xz fix.patch\"\n"
               "build() { :; }\n");
    write_file(origin / "fix.patch", "captured patch\n");
    write_file(origin / ".md5sum",
               "00000000000000000000000000000000  renamed.tar.xz\n"
               "11111111111111111111111111111111  fix.patch\n");
    write_file(origin / ".nostrip", "^usr/(bin|sbin)/\n");
    write_file(origin / ".32bit", "");
    write_file(origin / ".footprint",
               "drwxr-xr-x\troot/root\tusr/\n");

    pkgsource::pkgfile_backend backend;
    auto snapshot = backend.inspect({
        pkgsource::source_location(origin),
        std::nullopt,
        {},
    });

    const std::string fingerprint = snapshot.fingerprint().hex();
    const auto captured_root = snapshot.native_root();
    auto compare_path_snapshot = snapshot;
    auto write_path_snapshot = snapshot;

    fs::remove_all(origin);

    pkgbuild::AcceptedBuildPolicy policy;
    policy.archive = {
        pkgbuild::ArchiveFormat::pax,
        pkgbuild::Compression::xz,
    };
    policy.transformations = {false, true};
    policy.footprint.action = pkgbuild::FootprintAction::compare;

    const auto definition =
        pkgbuild::derive_definition(std::move(snapshot), policy);

    require(definition.source_snapshot_fingerprint().hex() == fingerprint,
            "snapshot fingerprint was not retained");
    require(&definition.source_description() ==
                &definition.snapshot().build(),
            "normalized source result was copied out of its snapshot");
    require(definition.identity().name == "sample" &&
                definition.identity().version == "1.2" &&
                definition.identity().release == "3",
            "package identity projection failed");

    require(definition.sources().size() == 2,
            "source input projection failed");
    const auto& remote = definition.sources()[0];
    require(remote.input.declaration ==
                "renamed.tar.xz::https://example.invalid/upstream.tar.xz" &&
                remote.input.uri &&
                *remote.input.uri ==
                    "https://example.invalid/upstream.tar.xz" &&
                remote.input.local_name == "renamed.tar.xz" &&
                !remote.captured,
            "remote source projection failed");
    require(remote.input.digests.size() == 1 &&
                remote.input.digests[0].algorithm ==
                    pkgbuild::DigestAlgorithm::md5,
            "remote digest projection failed");

    const auto& local = definition.sources()[1];
    require(!local.input.uri && local.captured,
            "recipe-local source lost its captured object");
    require(local.captured->relative_path() == "fix.patch" &&
                local.captured->native_path() == captured_root / "fix.patch" &&
                fs::is_regular_file(local.captured->native_path()),
            "recipe-local source escaped the source snapshot");

    require(definition.recipe().program().native_path() ==
                captured_root / "Pkgfile" &&
                fs::is_regular_file(definition.recipe().program().native_path()),
            "captured recipe identity was not retained");
    require(definition.strip_exclusions().size() == 1 &&
                definition.strip_exclusions()[0].syntax ==
                    pkgbuild::StripPatternSyntax::
                        posix_extended_regular_expression &&
                definition.strip_exclusions()[0].pattern ==
                    "^usr/(bin|sbin)/",
            "strip exclusion projection failed");
    require(definition.footprint() &&
                definition.footprint()->file().native_path() ==
                    captured_root / ".footprint",
            "captured footprint was not retained");
    require(definition.architecture() ==
                pkgbuild::BuildArchitecture::legacy_32bit,
            "architecture projection failed");
    require(definition.policy().archive.format ==
                pkgbuild::ArchiveFormat::pax &&
                definition.policy().archive.compression ==
                    pkgbuild::Compression::xz &&
                !definition.policy().transformations.strip_binaries &&
                definition.policy().transformations.compress_manpages &&
                definition.policy().footprint.action ==
                    pkgbuild::FootprintAction::compare,
            "accepted build policy was not retained");

    pkgbuild::AcceptedBuildPolicy compare_path_policy;
    compare_path_policy.footprint.action =
        pkgbuild::FootprintAction::compare;
    compare_path_policy.footprint.manifest = "/tmp/second-autopsy";
    expect_pkgbuild_error(pkgbuild::ErrorCode::invalid_configuration, [&] {
        (void)pkgbuild::derive_definition(
            std::move(compare_path_snapshot), compare_path_policy);
    });

    pkgbuild::AcceptedBuildPolicy write_path_policy;
    write_path_policy.footprint.action = pkgbuild::FootprintAction::write;
    write_path_policy.footprint.manifest = "relative-footprint";
    expect_pkgbuild_error(pkgbuild::ErrorCode::invalid_configuration, [&] {
        (void)pkgbuild::derive_definition(
            std::move(write_path_snapshot), write_path_policy);
    });

    return 0;
}
