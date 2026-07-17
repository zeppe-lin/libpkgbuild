#include <pkgbuild/backends/openssl.hpp>
#include <pkgbuild/engine.hpp>
#include <pkgbuild/error.hpp>
#include <pkgbuild/stage.hpp>

#include <cerrno>
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
        require(error.code() == code,
                "unexpected pkgbuild error code: " +
                    std::string(error.what()));
        return;
    }
    fail("expected pkgbuild error");
}

std::filesystem::path temporary_directory()
{
    std::string pattern = "/tmp/libpkgbuild-integrity.XXXXXX";
    std::vector<char> storage(pattern.begin(), pattern.end());
    storage.push_back('\0');
    char* result = mkdtemp(storage.data());
    if (!result)
        fail("mkdtemp failed");
    return result;
}

void write_file(const std::filesystem::path& path, const std::string& value)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        fail("cannot write test file");
    output << value;
}

std::string read_descriptor(const pkgbuild::VerifiedSource& source)
{
    const int descriptor = source.duplicate_descriptor();
    if (lseek(descriptor, 0, SEEK_SET) < 0) {
        close(descriptor);
        fail("cannot rewind verified descriptor");
    }
    std::string result;
    char buffer[128];
    for (;;) {
        const ssize_t count = read(descriptor, buffer, sizeof(buffer));
        if (count > 0) {
            result.append(buffer, static_cast<std::size_t>(count));
            continue;
        }
        if (count == 0)
            break;
        if (errno == EINTR)
            continue;
        close(descriptor);
        fail("cannot read verified descriptor");
    }
    close(descriptor);
    return result;
}

pkgbuild::BuildIdentity nobody_identity()
{
    std::vector<char> buffer(16384);
    struct passwd record {};
    struct passwd* result = nullptr;
    if (getpwnam_r("nobody", &record, buffer.data(), buffer.size(), &result) !=
            0 ||
        !result)
        fail("nobody user is required for root integrity tests");

    int count = 0;
    (void)getgrouplist(record.pw_name, record.pw_gid, nullptr, &count);
    std::vector<gid_t> groups(static_cast<std::size_t>(count));
    if (count != 0 &&
        getgrouplist(record.pw_name, record.pw_gid, groups.data(), &count) < 0)
        fail("cannot read nobody supplementary groups");
    groups.resize(static_cast<std::size_t>(count));
    return {record.pw_uid, record.pw_gid, std::move(groups),
            record.pw_dir ? record.pw_dir : "/", record.pw_name};
}

class Definitions final : public pkgbuild::DefinitionLoader {
public:
    std::string_view name() const noexcept override { return "test"; }
    pkgbuild::PackageDefinition load(const pkgbuild::DefinitionRequest&,
                                     pkgbuild::EventSink&) const override
    {
        return definition;
    }
    pkgbuild::PackageDefinition definition;
};

class Downloader final : public pkgbuild::Downloader {
public:
    std::string_view name() const noexcept override { return "test"; }
    pkgbuild::DownloadReceipt fetch(const pkgbuild::DownloadRequest& request,
                                    pkgbuild::EventSink&) const override
    {
        ++calls;
        write_file(request.destination, payload);
        return {request.uri, request.destination, payload.size(), false};
    }
    std::string payload;
    mutable int calls{0};
};

class Extractor final : public pkgbuild::SourceExtractor {
public:
    enum class Mode { normal, replace_path, mutate_source };

    std::string_view name() const noexcept override { return "test"; }
    void extract(const pkgbuild::ExtractRequest& request,
                 pkgbuild::EventSink&) const override
    {
        ++calls;
        observed = read_descriptor(request.source);
        if (mode == Mode::replace_path) {
            const auto retained = request.source.path().string() + ".retained";
            std::filesystem::rename(request.source.path(), retained);
            write_file(request.source.path(), "replacement");
        } else if (mode == Mode::mutate_source) {
            write_file(request.source.path(), "mutated");
        }
        write_file(request.destination / "extracted", observed);
    }

    mutable int calls{0};
    mutable std::string observed;
    Mode mode{Mode::normal};
};

class Recipes final : public pkgbuild::RecipeRunner {
public:
    std::string_view name() const noexcept override { return "test"; }
    pkgbuild::StagedPackage run(const pkgbuild::RecipeRequest& request,
                                pkgbuild::EventSink&) const override
    {
        ++calls;
        if (!expected_source.empty()) {
            std::ifstream input(request.source_root / expected_source,
                                std::ios::binary);
            std::string value((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
            require(value == expected_payload,
                    "recipe observed wrong verified source payload");
        }
        write_file(request.package_root / "usr/share/result", "ok");
        return pkgbuild::scan_staged_package(request.package_root);
    }

    mutable int calls{0};
    std::filesystem::path expected_source;
    std::string expected_payload;
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
        ++calls;
        write_file(request.output, "archive");
        return {request.output, 7, request.archive};
    }
    mutable int calls{0};
};

pkgbuild::BuildRequest make_request(const std::filesystem::path& root,
                                    bool download)
{
    pkgbuild::ExecutionPolicy execution;
    execution.environment = {{"PATH", "/usr/bin:/bin"}, {"LANG", "C"}};
    if (geteuid() == 0)
        execution.identity = nobody_identity();
    return {
        {{root / "recipe", root / "sources", root / "packages",
          root / "work"},
         std::nullopt, {}, std::move(execution)},
        download,
        false,
    };
}

pkgbuild::PackageDefinition definition_for(pkgbuild::Source source)
{
    return {{"integrity", "1", "1"},
            {std::move(source)},
            {pkgbuild::RecipeFormat::pkgfile_v0, {}, std::nullopt, "build"},
            {}};
}

struct Harness {
    Definitions definitions;
    Downloader downloader;
    pkgbuild::OpenSslSourceVerifier verifier;
    Extractor extractor;
    Recipes recipes;
    Packages packages;
    pkgbuild::NullEventSink events;

    pkgbuild::Engine engine()
    {
        return pkgbuild::Engine({definitions, downloader, verifier, extractor,
                                 recipes, packages});
    }
};

} // namespace

int main()
{
    std::filesystem::path root;
    try {
        root = temporary_directory();
        std::filesystem::create_directories(root / "recipe");

        {
            Harness harness;
            harness.definitions.definition = definition_for(
                {"payload.txt", std::nullopt, "payload.txt",
                 {{pkgbuild::DigestAlgorithm::md5,
                   "900150983cd24fb0d6963f7d28e17f72"}}});
            write_file(root / "recipe/payload.txt", "abc");
            harness.recipes.expected_source = "payload.txt";
            harness.recipes.expected_payload = "abc";
            auto engine = harness.engine();
            const auto receipt = engine.build(make_request(root, false),
                                              harness.events);
            require(receipt.verifications.size() == 1,
                    "verification receipt was not propagated");
            require(harness.downloader.calls == 0,
                    "cached local source was downloaded");
            require(harness.recipes.calls == 1,
                    "valid local source did not reach recipe");
        }

        std::filesystem::remove_all(root / "work");
        std::filesystem::remove_all(root / "packages");
        {
            Harness harness;
            harness.definitions.definition = definition_for(
                {"bad.txt", std::nullopt, "bad.txt",
                 {{pkgbuild::DigestAlgorithm::md5,
                   "00000000000000000000000000000000"}}});
            write_file(root / "recipe/bad.txt", "abc");
            auto engine = harness.engine();
            require_error(pkgbuild::ErrorCode::checksum_mismatch, [&] {
                (void)engine.build(make_request(root, false), harness.events);
            });
            require(harness.recipes.calls == 0 && harness.packages.calls == 0,
                    "checksum mismatch reached recipe or package writer");
        }

        std::filesystem::remove_all(root / "work");
        std::filesystem::remove_all(root / "packages");
        std::filesystem::remove_all(root / "sources");
        {
            Harness harness;
            harness.downloader.payload = "downloaded";
            harness.definitions.definition = definition_for(
                {"remote.tar::https://example.invalid/source.tar",
                 "https://example.invalid/source.tar", "remote.tar",
                 {{pkgbuild::DigestAlgorithm::md5,
                   "56f18825a76309ae6391073aeb14f1b3"}}});
            harness.recipes.expected_source = "extracted";
            harness.recipes.expected_payload = "downloaded";
            auto engine = harness.engine();
            const auto receipt =
                engine.build(make_request(root, true), harness.events);
            require(receipt.downloads.size() == 1 &&
                        harness.downloader.calls == 1,
                    "downloaded source receipt was not recorded");
            require(harness.extractor.observed == "downloaded",
                    "downloaded source was not verified before extraction");
        }

        std::filesystem::remove_all(root / "work");
        std::filesystem::remove_all(root / "packages");
        std::filesystem::remove_all(root / "sources");
        {
            Harness harness;
            harness.definitions.definition = definition_for(
                {"missing-digest", std::nullopt, "missing-digest", {}});
            write_file(root / "recipe/missing-digest", "abc");
            auto engine = harness.engine();
            require_error(pkgbuild::ErrorCode::invalid_definition, [&] {
                (void)engine.build(make_request(root, false), harness.events);
            });
            require(harness.recipes.calls == 0,
                    "source without checksum reached recipe");
        }

        std::filesystem::remove_all(root / "work");
        std::filesystem::remove_all(root / "packages");
        {
            Harness harness;
            harness.extractor.mode = Extractor::Mode::replace_path;
            harness.definitions.definition = definition_for(
                {"replace.tar", std::nullopt, "replace.tar",
                 {{pkgbuild::DigestAlgorithm::md5,
                   "11c2eaf8b15796c367385dc37bc11c5e"}}});
            write_file(root / "recipe/replace.tar", "original-archive");
            harness.recipes.expected_source = "extracted";
            harness.recipes.expected_payload = "original-archive";
            auto engine = harness.engine();
            (void)engine.build(make_request(root, false), harness.events);
            require(harness.extractor.observed == "original-archive",
                    "pathname replacement substituted unverified bytes");
        }

        std::filesystem::remove_all(root / "work");
        std::filesystem::remove_all(root / "packages");
        {
            Harness harness;
            harness.extractor.mode = Extractor::Mode::mutate_source;
            harness.definitions.definition = definition_for(
                {"mutate.tar", std::nullopt, "mutate.tar",
                 {{pkgbuild::DigestAlgorithm::md5,
                   "cc6c7fb152e6e7d0684864c09eecad4b"}}});
            write_file(root / "recipe/mutate.tar", "mutate-me");
            auto engine = harness.engine();
            require_error(pkgbuild::ErrorCode::source_changed, [&] {
                (void)engine.build(make_request(root, false), harness.events);
            });
            require(harness.recipes.calls == 0,
                    "source mutation reached recipe execution");
        }

        std::filesystem::remove_all(root);
        std::cout << "source integrity: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        if (!root.empty())
            std::filesystem::remove_all(root);
        std::cerr << "source integrity: " << error.what() << '\n';
        return 1;
    }
}
