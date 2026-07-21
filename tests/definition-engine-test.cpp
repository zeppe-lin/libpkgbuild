#include <pkgbuild/backends/openssl.hpp>
#include <pkgbuild/definition.hpp>
#include <pkgbuild/engine.hpp>
#include <pkgbuild/stage.hpp>

#include <libpkgsource/pkgfile_backend.h>

#include <filesystem>
#include <fstream>
#include <grp.h>
#include <iostream>
#include <pwd.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <unistd.h>
#include <vector>

namespace {
namespace fs = std::filesystem;

[[noreturn]] void fail(const std::string& message)
{
    throw std::runtime_error(message);
}

void require(bool value, const std::string& message)
{
    if (!value)
        fail(message);
}

class TemporaryDirectory final {
public:
    TemporaryDirectory()
    {
        std::string pattern =
            (fs::temp_directory_path() / "pkgbuild-definition.XXXXXX").string();
        std::vector<char> storage(pattern.begin(), pattern.end());
        storage.push_back('\0');
        char* made = mkdtemp(storage.data());
        if (made == nullptr)
            fail("cannot create temporary directory");
        path_ = made;
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
        fail("cannot create " + path.string());
    output << contents;
    if (!output)
        fail("cannot write " + path.string());
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
    return pkgbuild::BuildIdentity{
        record.pw_uid, record.pw_gid, std::move(groups),
        record.pw_dir ? record.pw_dir : "/", record.pw_name};
}

class Downloader final : public pkgbuild::Downloader {
public:
    std::string_view name() const noexcept override { return "unused"; }
    pkgbuild::DownloadReceipt fetch(const pkgbuild::DownloadRequest&,
                                    pkgbuild::EventSink&) const override
    {
        fail("captured local input attempted a download");
    }
};

class Extractor final : public pkgbuild::SourceExtractor {
public:
    std::string_view name() const noexcept override { return "unused"; }
    void extract(const pkgbuild::ExtractRequest&,
                 pkgbuild::EventSink&) const override
    {
        fail("plain captured local input attempted extraction");
    }
};

class Recipes final : public pkgbuild::RecipeRunner {
public:
    explicit Recipes(fs::path deleted_origin)
        : deleted_origin_(std::move(deleted_origin)) {}

    std::string_view name() const noexcept override { return "test"; }

    pkgbuild::StagedPackage run(const pkgbuild::RecipeRequest&,
                                pkgbuild::EventSink&) const override
    {
        fail("legacy recipe request was used");
    }

    pkgbuild::StagedPackage run_captured(
        const pkgbuild::CapturedRecipeRequest& request,
        pkgbuild::EventSink&) const override
    {
        require(!fs::exists(deleted_origin_),
                "original package directory returned during execution");
        require(request.paths.recipe_dir != deleted_origin_,
                "recipe executed from the original package directory");
        require(fs::is_regular_file(request.paths.recipe_dir / "Pkgfile"),
                "captured Pkgfile was not materialized");
        require(fs::is_regular_file(request.paths.recipe_dir / "payload.txt"),
                "captured sibling was not materialized");
        struct stat recipe_state {};
        require(lstat(request.paths.recipe_dir.c_str(), &recipe_state) == 0,
                "cannot inspect materialized recipe root");
        require((recipe_state.st_mode & 0222) == 0,
                "materialized recipe root remained writable");

        std::ifstream payload(request.source_root / "payload.txt");
        std::string line;
        std::getline(payload, line);
        require(line == "captured payload",
                "prepared source did not come from captured input");
        require(request.definition.identity().name == "sealed-engine",
                "wrong build definition reached recipe runner");

        fs::create_directories(request.package_root / "usr/bin");
        write_file(request.package_root / "usr/bin/sealed", "built\n");
        return pkgbuild::scan_staged_package(request.package_root);
    }

private:
    fs::path deleted_origin_;
};

class Transformer final : public pkgbuild::PackageTransformer {
public:
    std::string_view name() const noexcept override { return "test"; }

    pkgbuild::TransformationReceipt transform(
        const pkgbuild::PackageTransformRequest&,
        pkgbuild::EventSink&) const override
    {
        fail("legacy transformation request was used");
    }

    pkgbuild::TransformationReceipt transform_definition(
        const pkgbuild::DefinitionTransformRequest& request,
        pkgbuild::EventSink&) const override
    {
        require(request.definition.strip_exclusions().size() == 1,
                "typed strip exclusions were not retained");
        require(request.definition.strip_exclusions().front().syntax ==
                    pkgbuild::StripPatternSyntax::
                        posix_extended_regular_expression,
                "strip exclusion grammar was lost");
        return {std::string(name()), {}};
    }
};

class Packages final : public pkgbuild::PackageWriter {
public:
    std::string_view name() const noexcept override { return "test"; }

    bool supports(const pkgbuild::ArchiveSpec& archive) const noexcept override
    {
        return archive.format == pkgbuild::ArchiveFormat::pax &&
               archive.compression == pkgbuild::Compression::xz;
    }

    pkgbuild::ArchiveReceipt write(
        const pkgbuild::PackageWriteRequest& request,
        pkgbuild::EventSink&) const override
    {
        require(fs::is_regular_file(request.package.root / "usr/bin/sealed"),
                "staged package was lost before archive writing");
        fs::create_directories(request.output.parent_path());
        write_file(request.output, "artifact\n");
        return {request.output, fs::file_size(request.output), request.archive};
    }
};

} // namespace

int main()
{
    try {
        TemporaryDirectory temporary;
        const auto origin = temporary.path() / "sealed-engine";
        fs::create_directories(origin);
        write_file(origin / "Pkgfile",
                   "name=sealed-engine\n"
                   "version=1.2\n"
                   "release=3\n"
                   "source=payload.txt\n"
                   "build() { :; }\n");
        write_file(origin / "payload.txt", "captured payload\n");
        write_file(origin / ".md5sum",
                   "bfcffaf10d666fd226ea0bdbe1cf00f2  payload.txt\n");
        write_file(origin / ".nostrip", "^usr/(bin|sbin)/\n");

        pkgsource::pkgfile_backend source_backend;
        auto snapshot = source_backend.inspect({
            pkgsource::source_location(origin), std::nullopt, {}});
        const auto fingerprint = snapshot.fingerprint().hex();

        pkgbuild::AcceptedBuildPolicy policy;
        policy.archive = {
            pkgbuild::ArchiveFormat::pax,
            pkgbuild::Compression::xz,
        };
        policy.transformations = {false, false};
        const auto definition = pkgbuild::derive_definition(
            std::move(snapshot), policy);

        fs::remove_all(origin);

        Downloader downloader;
        pkgbuild::OpenSslSourceVerifier verifier;
        Extractor extractor;
        Recipes recipes(origin);
        Transformer transformer;
        Packages packages;
        pkgbuild::Engine engine(pkgbuild::BuildServices{
            downloader, verifier, extractor, recipes, transformer, packages});
        pkgbuild::NullEventSink events;
        std::optional<pkgbuild::BuildIdentity> identity;
        if (geteuid() == 0)
            identity = nobody_identity();

        const auto receipt = engine.build(
            definition,
            pkgbuild::BuildEnvironment{
                temporary.path() / "sources",
                temporary.path() / "packages",
                temporary.path() / "work",
                pkgbuild::ExecutionPolicy{
                    identity,
                    {{"PATH", "/usr/bin:/bin"}, {"LANG", "C"}},
                    0022,
                    std::nullopt,
                },
                false,
                false,
                std::nullopt,
            },
            events);

        require(receipt.definition.source_snapshot_fingerprint().hex() ==
                    fingerprint,
                "receipt lost source snapshot provenance");
        require(receipt.definition.identity().name == "sealed-engine" &&
                    receipt.definition.identity().version == "1.2" &&
                    receipt.definition.identity().release == "3",
                "receipt lost package release identity");
        require(receipt.package == receipt.archive.output,
                "artifact path was reconstructed after writing");
        require(receipt.package.filename() ==
                    "sealed-engine#1.2-3.pkg.tar.xz",
                "accepted artifact identity was not used");
        require(fs::is_regular_file(receipt.archive.output),
                "reported artifact does not exist");
        require(receipt.verifications.size() == 1,
                "captured local source was not verified");
        require(fs::is_empty(temporary.path() / "work"),
                "discarded sealed workspace survived cleanup");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "definition-engine: " << error.what() << '\n';
        return 1;
    }
}
