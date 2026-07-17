#include <pkgbuild/backend.hpp>
#include <pkgbuild/engine.hpp>
#include <pkgbuild/error.hpp>
#include <pkgbuild/footprint.hpp>
#include <pkgbuild/stage.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <grp.h>
#include <iostream>
#include <optional>
#include <pwd.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

[[noreturn]] void fail(const std::string& message)
{
    std::cerr << "footprint-pipeline-test: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const std::string& message)
{
    if (!condition)
        fail(message);
}

class TemporaryDirectory final {
public:
    TemporaryDirectory()
    {
        std::string pattern = "/tmp/pkgbuild-footprint-pipeline.XXXXXX";
        std::vector<char> storage(pattern.begin(), pattern.end());
        storage.push_back('\0');
        char* result = mkdtemp(storage.data());
        if (result == nullptr)
            fail("cannot create temporary directory");
        path_ = result;
    }
    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    const std::filesystem::path& path() const noexcept { return path_; }
private:
    std::filesystem::path path_;
};

pkgbuild::BuildIdentity nobody_identity()
{
    std::vector<char> buffer(16384);
    passwd record {};
    passwd* result = nullptr;
    if (getpwnam_r("nobody", &record, buffer.data(), buffer.size(), &result) != 0 ||
        result == nullptr)
        fail("nobody user is unavailable");
    return {record.pw_uid, record.pw_gid, {record.pw_gid},
            record.pw_dir ? record.pw_dir : "/", record.pw_name};
}

void write_file(const std::filesystem::path& path, const std::string& data)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << data;
    if (!output)
        fail("cannot write " + path.string());
}

class Definitions final : public pkgbuild::DefinitionLoader {
public:
    std::string_view name() const noexcept override { return "test"; }
    pkgbuild::PackageDefinition load(const pkgbuild::DefinitionRequest& request,
                                     pkgbuild::EventSink&) const override
    {
        return {{"footprint", "1", "1"}, {},
                {pkgbuild::RecipeFormat::pkgfile_v0,
                 request.paths.recipe_dir / "Pkgfile", std::nullopt, "build"},
                {}, {}};
    }
};

class Downloader final : public pkgbuild::Downloader {
public:
    std::string_view name() const noexcept override { return "unused"; }
    pkgbuild::DownloadReceipt fetch(const pkgbuild::DownloadRequest&,
                                    pkgbuild::EventSink&) const override
    {
        throw std::runtime_error("downloader called unexpectedly");
    }
};

class Verifier final : public pkgbuild::SourceVerifier {
public:
    std::string_view name() const noexcept override { return "unused"; }
    pkgbuild::VerifiedSource verify(const std::filesystem::path&,
                                    const std::vector<pkgbuild::Digest>&,
                                    pkgbuild::EventSink&) const override
    {
        throw std::runtime_error("verifier called unexpectedly");
    }
    void revalidate(const pkgbuild::VerifiedSource&,
                    pkgbuild::EventSink&) const override
    {
        throw std::runtime_error("verifier called unexpectedly");
    }
};

class Extractor final : public pkgbuild::SourceExtractor {
public:
    std::string_view name() const noexcept override { return "unused"; }
    void extract(const pkgbuild::ExtractRequest&,
                 pkgbuild::EventSink&) const override
    {
        throw std::runtime_error("extractor called unexpectedly");
    }
};

class Recipes final : public pkgbuild::RecipeRunner {
public:
    std::string_view name() const noexcept override { return "test"; }
    pkgbuild::StagedPackage run(const pkgbuild::RecipeRequest& request,
                                pkgbuild::EventSink&) const override
    {
        const auto bin = request.package_root / "usr/bin";
        std::filesystem::create_directories(bin);
        const auto tool = bin / "tool";
        write_file(tool, "payload\n");
        std::filesystem::permissions(
            tool, changed ? std::filesystem::perms::owner_read
                          : std::filesystem::perms::owner_all |
                                std::filesystem::perms::group_read |
                                std::filesystem::perms::group_exec |
                                std::filesystem::perms::others_read |
                                std::filesystem::perms::others_exec,
            std::filesystem::perm_options::replace);
        if (changed)
            write_file(request.package_root / "usr/share/new", "new\n");
        return pkgbuild::scan_staged_package(request.package_root);
    }
    mutable bool changed{false};
};

class Writer final : public pkgbuild::PackageWriter {
public:
    std::string_view name() const noexcept override { return "test"; }
    bool supports(const pkgbuild::ArchiveSpec&) const noexcept override
    {
        return true;
    }
    pkgbuild::ArchiveReceipt write(const pkgbuild::PackageWriteRequest& request,
                                   pkgbuild::EventSink&) const override
    {
        ++calls;
        write_file(request.output, "archive\n");
        return {request.output, 8, request.archive};
    }
    mutable int calls{0};
};

pkgbuild::BuildRequest request_for(const std::filesystem::path& root,
                                   pkgbuild::FootprintAction action,
                                   const std::filesystem::path& manifest)
{
    pkgbuild::ExecutionPolicy execution;
    execution.environment = {{"PATH", "/usr/bin:/bin"}, {"LANG", "C"}};
    if (geteuid() == 0)
        execution.identity = nobody_identity();
    return {
        {{root / "recipe", root / "sources", root / "packages",
          root / "work"}, std::nullopt, {}, std::move(execution)},
        false,
        false,
        {},
        {action, manifest},
    };
}

} // namespace

int main()
{
    TemporaryDirectory temporary;
    const auto root = temporary.path();
    std::filesystem::create_directories(root / "recipe");
    const auto manifest = root / "recipe/.footprint";

    Definitions definitions;
    Downloader downloader;
    Verifier verifier;
    Extractor extractor;
    Recipes recipes;
    pkgbuild::NullPackageTransformer transformer;
    Writer writer;
    pkgbuild::Engine engine({definitions, downloader, verifier, extractor,
                             recipes, transformer, writer});
    pkgbuild::NullEventSink events;

    const auto written = engine.build(
        request_for(root, pkgbuild::FootprintAction::write, manifest), events);
    require(written.footprint.has_value(), "write receipt is missing");
    require(written.footprint->written, "write receipt is not marked written");
    require(std::filesystem::exists(manifest), "manifest was not written");
    require(writer.calls == 1, "archive was not written after footprint write");

    const auto compared = engine.build(
        request_for(root, pkgbuild::FootprintAction::compare, manifest), events);
    require(compared.footprint.has_value(), "compare receipt is missing");
    require(compared.footprint->expected.has_value(),
            "expected footprint was not retained");
    require(compared.footprint->difference.empty(),
            "matching footprint reported differences");
    require(writer.calls == 2,
            "archive was not written after successful comparison");

    recipes.changed = true;
    try {
        (void)engine.build(
            request_for(root, pkgbuild::FootprintAction::compare, manifest),
            events);
        fail("footprint mismatch did not stop the build");
    } catch (const pkgbuild::FootprintMismatch& mismatch) {
        require(mismatch.manifest() == std::filesystem::absolute(manifest),
                "mismatch reported the wrong manifest");
        require(mismatch.difference().added.size() == 2,
                "added paths were not retained in mismatch");
        require(mismatch.difference().changed.size() == 1,
                "changed path was not retained in mismatch");
    }
    require(writer.calls == 2,
            "archive writer ran after footprint mismatch");

    try {
        (void)engine.build(
            request_for(root, pkgbuild::FootprintAction::compare,
                        root / "recipe/missing"), events);
        fail("missing footprint did not stop the build");
    } catch (const pkgbuild::Error& error) {
        require(error.code() == pkgbuild::ErrorCode::filesystem_failed,
                "missing footprint produced the wrong error");
    }
    require(writer.calls == 2,
            "archive writer ran after missing footprint");

    std::cout << "footprint pipeline: PASS\n";
    return 0;
}
