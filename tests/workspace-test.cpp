#include <pkgbuild/engine.hpp>
#include <pkgbuild/error.hpp>
#include <pkgbuild/stage.hpp>

#include <filesystem>
#include <fstream>
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
    std::string pattern = "/tmp/libpkgbuild-workspace.XXXXXX";
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
        fail("nobody user is required for root workspace tests");

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

class Definitions final : public pkgbuild::DefinitionLoader {
public:
    std::string_view name() const noexcept override { return "test"; }

    pkgbuild::PackageDefinition load(const pkgbuild::DefinitionRequest& request,
                                     pkgbuild::EventSink&) const override
    {
        inspected.push_back(request.paths.work_dir);
        return pkgbuild::PackageDefinition{
            {"workspace", "1", "1"},
            {},
            {pkgbuild::RecipeFormat::pkgfile_v0, {}, std::nullopt, "build"},
            {},
            {},
        };
    }

    mutable std::vector<std::filesystem::path> inspected;
};

class Downloader final : public pkgbuild::Downloader {
public:
    std::string_view name() const noexcept override { return "unused"; }
    pkgbuild::DownloadReceipt fetch(const pkgbuild::DownloadRequest&,
                                    pkgbuild::EventSink&) const override
    {
        fail("downloader must not be called");
    }
};

class Verifier final : public pkgbuild::SourceVerifier {
public:
    std::string_view name() const noexcept override { return "unused"; }

    pkgbuild::VerifiedSource verify(
        const std::filesystem::path&,
        const std::vector<pkgbuild::Digest>&,
        pkgbuild::EventSink&) const override
    {
        fail("verifier must not be called");
    }

    void revalidate(const pkgbuild::VerifiedSource&,
                    pkgbuild::EventSink&) const override
    {
        fail("verifier must not be called");
    }
};

class Extractor final : public pkgbuild::SourceExtractor {
public:
    std::string_view name() const noexcept override { return "unused"; }
    void extract(const pkgbuild::ExtractRequest&,
                 pkgbuild::EventSink&) const override
    {
        fail("extractor must not be called");
    }
};

class Recipes final : public pkgbuild::RecipeRunner {
public:
    std::string_view name() const noexcept override { return "test"; }

    pkgbuild::StagedPackage run(const pkgbuild::RecipeRequest& request,
                                pkgbuild::EventSink&) const override
    {
        workspaces.push_back(request.paths.work_dir);
        struct stat state {};
        require(lstat(request.paths.work_dir.c_str(), &state) == 0,
                "workspace does not exist");
        require((state.st_mode & 0777) == 0700,
                "workspace is not mode 0700");
        if (request.execution.identity && geteuid() == 0) {
            require(state.st_uid == request.execution.identity->uid,
                    "workspace has the wrong user owner");
            require(state.st_gid == request.execution.identity->gid,
                    "workspace has the wrong group owner");
        }

        std::filesystem::create_directories(request.package_root / "usr/bin");
        std::ofstream(request.package_root / "usr/bin/workspace") << "ok\n";
        return pkgbuild::scan_staged_package(request.package_root);
    }

    mutable std::vector<std::filesystem::path> workspaces;
};

class Packages final : public pkgbuild::PackageWriter {
public:
    std::string_view name() const noexcept override { return "test"; }
    bool supports(const pkgbuild::ArchiveSpec&) const noexcept override
    {
        return true;
    }

    pkgbuild::ArchiveReceipt write(const pkgbuild::PackageWriteRequest& request,
                                   pkgbuild::EventSink&) const override
    {
        require(std::filesystem::exists(request.package.root / "usr/bin/workspace"),
                "recipe output was not staged");
        std::filesystem::create_directories(request.output.parent_path());
        std::ofstream(request.output) << "archive\n";
        return {request.output, std::filesystem::file_size(request.output),
                request.archive};
    }
};

pkgbuild::BuildRequest make_request(const std::filesystem::path& root,
                                    bool keep)
{
    pkgbuild::ExecutionPolicy execution;
    execution.environment = {{"PATH", "/usr/bin:/bin"}, {"LANG", "C"}};
    if (geteuid() == 0)
        execution.identity = nobody_identity();

    return pkgbuild::BuildRequest{
        pkgbuild::DefinitionRequest{
            pkgbuild::BuildPaths{
                root / "recipe",
                root / "sources",
                root / "packages",
                root / "work-base",
            },
            std::nullopt,
            {},
            std::move(execution),
        },
        false,
        keep,
        {},
    };
}

} // namespace

int main()
{
    try {
        const auto root = temporary_directory();
        std::filesystem::create_directories(root / "recipe");
        std::filesystem::create_directories(root / "work-base");
        std::ofstream(root / "work-base/sentinel") << "alive\n";

        Definitions definitions;
        Downloader downloader;
        Verifier verifier;
        Extractor extractor;
        Recipes recipes;
        Packages packages;
        pkgbuild::NullPackageTransformer transformer;
        pkgbuild::NullEventSink events;
        pkgbuild::Engine engine({definitions, downloader, verifier,
                                 extractor, recipes, transformer, packages});

        auto request = make_request(root, false);
        const auto first = engine.build(request, events);
        require(!first.work_directory, "discarded workspace was reported");
        require(std::filesystem::exists(root / "work-base/sentinel"),
                "workspace cleanup removed a sibling file");
        require(std::filesystem::is_empty(root / "work-base") == false,
                "sentinel unexpectedly disappeared");
        require(std::distance(std::filesystem::directory_iterator(root / "work-base"),
                              std::filesystem::directory_iterator{}) == 1,
                "discarded private workspace survived cleanup");

        (void)engine.build(request, events);
        require(recipes.workspaces.size() == 2,
                "recipe did not observe both workspaces");
        require(recipes.workspaces[0] != recipes.workspaces[1],
                "workspace name was reused");
        require(recipes.workspaces[0].parent_path() == root / "work-base" &&
                    recipes.workspaces[1].parent_path() == root / "work-base",
                "workspace escaped the configured base");

        auto retained = make_request(root, true);
        const auto kept = engine.build(retained, events);
        require(kept.work_directory.has_value(),
                "retained workspace was not reported");
        require(std::filesystem::is_directory(*kept.work_directory),
                "retained workspace was removed");
        std::filesystem::remove_all(*kept.work_directory);

        auto root_base = make_request(root, false);
        root_base.definition.paths.work_dir = "/";
        require_error(pkgbuild::ErrorCode::invalid_configuration, [&] {
            (void)engine.build(root_base, events);
        });

        const auto real_base = root / "real-work";
        const auto link_base = root / "linked-work";
        std::filesystem::create_directories(real_base);
        std::filesystem::create_directory_symlink(real_base, link_base);
        auto symlink_base = make_request(root, false);
        symlink_base.definition.paths.work_dir = link_base;
        require_error(pkgbuild::ErrorCode::invalid_configuration, [&] {
            (void)engine.build(symlink_base, events);
        });

        auto aliased = make_request(root, false);
        aliased.definition.paths.work_dir = aliased.definition.paths.source_dir;
        require_error(pkgbuild::ErrorCode::invalid_configuration, [&] {
            (void)engine.build(aliased, events);
        });

        if (geteuid() == 0) {
            auto no_identity = make_request(root, false);
            no_identity.definition.execution.identity.reset();
            require_error(pkgbuild::ErrorCode::invalid_configuration, [&] {
                (void)engine.build(no_identity, events);
            });
        }

        std::filesystem::remove_all(root);
        std::cout << "workspace: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "workspace: " << error.what() << '\n';
        return 1;
    }
}
