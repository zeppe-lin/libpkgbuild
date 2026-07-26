// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include <pkgbuild-plan/adapter.hpp>
#include <pkgbuild/backends/libarchive.hpp>
#include <pkgbuild/backends/openssl.hpp>
#include <pkgbuild/engine.hpp>
#include <pkgbuild/stage.hpp>

#include <libpkgimage/libarchive_backend.h>
#include <libpkgsource/pkgfile_backend.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#ifndef TEST_WORKER
#error TEST_WORKER is required
#endif

namespace fs = std::filesystem;

namespace {

struct temporary_directory final {
    fs::path path;
    temporary_directory()
    {
        std::string pattern =
            (fs::temp_directory_path() / "libpkgbuild-plan.XXXXXX").string();
        std::vector<char> bytes(pattern.begin(), pattern.end());
        bytes.push_back('\0');
        char* made = ::mkdtemp(bytes.data());
        if (made == nullptr)
            throw std::runtime_error("mkdtemp failed");
        path = made;
    }
    ~temporary_directory()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void write_file(const fs::path& path, const std::string& material)
{
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("cannot create fixture file");
    output.write(material.data(), static_cast<std::streamsize>(material.size()));
}

class downloader final : public pkgbuild::Downloader {
public:
    std::string_view name() const noexcept override { return "unused"; }
    pkgbuild::DownloadReceipt fetch(const pkgbuild::DownloadRequest&,
                                    pkgbuild::EventSink&) const override
    {
        throw std::runtime_error("unexpected download");
    }
};

class recipes final : public pkgbuild::RecipeRunner {
public:
    std::string_view name() const noexcept override { return "fixture"; }

    pkgbuild::StagedPackage run(const pkgbuild::RecipeRequest&,
                                pkgbuild::EventSink&) const override
    {
        throw std::runtime_error("legacy recipe path used");
    }

    pkgbuild::StagedPackage run_captured(
        const pkgbuild::CapturedRecipeRequest& request,
        pkgbuild::EventSink&) const override
    {
        write_file(request.package_root / "usr/bin/demo", "built payload\n");
        fs::permissions(request.package_root / "usr/bin/demo",
                        fs::perms::owner_exec | fs::perms::owner_read |
                            fs::perms::owner_write | fs::perms::group_read |
                            fs::perms::group_exec | fs::perms::others_read |
                            fs::perms::others_exec,
                        fs::perm_options::replace);
        write_file(request.package_root / "etc/demo.conf", "enabled=yes\n");
        return pkgbuild::scan_staged_package(request.package_root);
    }
};

pkgbuild::ExecutionPolicy execution_policy()
{
    pkgbuild::ExecutionPolicy policy;
    policy.environment = {{"PATH", "/usr/bin:/bin"}, {"LANG", "C"}};
    if (geteuid() == 0) {
        policy.identity = pkgbuild::BuildIdentity{
            65534, 65534, {}, "/tmp", "nobody"};
    }
    return policy;
}

pkgbuild::BuildReceipt build_fixture(const fs::path& root)
{
    const fs::path source = root / "demo";
    fs::create_directories(source);
    write_file(source / "Pkgfile",
               "name=demo\n"
               "version=1.2.3\n"
               "release=4\n"
               "source=payload.txt\n"
               "build() { :; }\n");
    write_file(source / "payload.txt", "source payload\n");
    write_file(source / ".md5sum",
               "12c97d6b33c58df1494fde961803aafb  payload.txt\n");

    pkgsource::pkgfile_backend source_backend(TEST_WORKER);
    auto snapshot = source_backend.inspect(
        {pkgsource::source_location(source), std::nullopt, {}});

    pkgbuild::AcceptedBuildPolicy policy;
    policy.archive = {pkgbuild::ArchiveFormat::gnutar,
                      pkgbuild::Compression::gzip};
    policy.transformations = {false, false};
    auto definition = pkgbuild::derive_definition(std::move(snapshot), policy);

    downloader downloads;
    pkgbuild::OpenSslSourceVerifier verifier;
    pkgbuild::LibarchiveBackend archives;
    recipes recipe;
    pkgbuild::NullPackageTransformer transformer;
    pkgbuild::Engine engine(pkgbuild::BuildServices{
        downloads, verifier, archives, recipe, transformer, archives});
    pkgbuild::NullEventSink events;

    return engine.build(
        definition,
        pkgbuild::BuildEnvironment{
            root / "sources", root / "packages", root / "work",
            execution_policy(), false, false, std::nullopt},
        events);
}

void expect_code(const pkgbuild::BuildReceipt& receipt,
                 pkgbuild::plan_adapter::projection_error_code code,
                 const pkgimage::archive_backend& archives)
{
    try {
        (void)pkgbuild::plan_adapter::project_artifact(receipt, archives);
    } catch (const pkgbuild::plan_adapter::projection_error& error) {
        require(error.code() == code, "wrong projection error code");
        return;
    }
    throw std::runtime_error("expected projection error");
}

} // namespace

int main()
{
    try {
        temporary_directory temporary;
        const pkgbuild::BuildReceipt receipt = build_fixture(temporary.path);
        pkgimage::libarchive_backend images;

        const auto first =
            pkgbuild::plan_adapter::project_artifact(receipt, images);
        const auto second =
            pkgbuild::plan_adapter::project_artifact(receipt, images);

        require(first.build().artifact.digest.hexadecimal ==
                    receipt.artifact.digest.hexadecimal,
                "projection lost build artifact seal");
        require(first.candidate().candidate().release().name() == "demo" &&
                    first.candidate().candidate().release().version() == "1.2.3" &&
                    first.candidate().candidate().release().release() == "4",
                "projection lost source release");
        require(first.artifact().release() ==
                    first.candidate().candidate().release(),
                "artifact release is not source-bound");
        require(first.artifact().artifact().string() ==
                    "v1:sha256:" + receipt.artifact.digest.hexadecimal,
                "artifact identity is not exact archive bytes");
        require(first.image().receipt().archive_digest().string() ==
                    first.artifact().artifact().string(),
                "archive inspection and artifact identity diverged");
        require(first.image().image().find(
                    pkgimage::package_path::parse("usr/bin/demo")) != nullptr,
                "normalized image lost executable payload");
        require(first.image().image().find(
                    pkgimage::package_path::parse("etc/demo.conf")) != nullptr,
                "normalized image lost configuration payload");
        require(first.artifact().manifest() == second.artifact().manifest(),
                "artifact manifest identity is unstable");

        auto inconsistent = receipt;
        ++inconsistent.artifact.bytes;
        expect_code(inconsistent,
                    pkgbuild::plan_adapter::projection_error_code::build_receipt,
                    images);

        std::ofstream changed(receipt.artifact.path,
                              std::ios::binary | std::ios::app);
        changed << "foreign bytes";
        changed.close();
        expect_code(receipt,
                    pkgbuild::plan_adapter::projection_error_code::archive_inspection,
                    images);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "plan-adapter-test: " << error.what() << '\n';
        return 1;
    }
}
