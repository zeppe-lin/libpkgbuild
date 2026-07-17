#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>

namespace pkgbuild {

enum class RecipeFormat {
    pkgfile_v0,
    recipe_yaml_v1,
};

enum class ArchiveFormat {
    gnutar,
    pax,
    ustar,
    v7,
};

enum class Compression {
    gzip,
    bzip2,
    xz,
    lzip,
    zstd,
};

enum class DigestAlgorithm {
    md5,
    sha256,
    sha512,
    blake2b512,
};

enum class FootprintAction {
    ignore,
    compare,
    write,
};

enum class TransformationKind {
    strip_binary,
    compress_manpage,
    rewrite_manpage_symlink,
};

struct Digest {
    DigestAlgorithm algorithm{DigestAlgorithm::sha256};
    std::string hexadecimal;
};

struct ArchiveSpec {
    ArchiveFormat format{ArchiveFormat::gnutar};
    Compression compression{Compression::gzip};
};

struct PackageId {
    std::string name;
    std::string version;
    std::string release;
};

struct Source {
    std::string declaration;
    std::optional<std::string> uri;
    std::filesystem::path local_name;
    std::vector<Digest> digests;
};

struct Recipe {
    RecipeFormat format{RecipeFormat::pkgfile_v0};
    std::filesystem::path file;
    std::optional<std::filesystem::path> config_file;
    std::string entrypoint{"build"};
};

struct PackageDefinition {
    PackageId id;
    std::vector<Source> sources;
    Recipe recipe;
    ArchiveSpec archive;
    std::vector<std::string> strip_exclusions;
};

struct BuildIdentity {
    uid_t uid{0};
    gid_t gid{0};
    std::vector<gid_t> supplementary_groups;
    std::filesystem::path home;
    std::string user;
};

struct ExecutionPolicy {
    std::optional<BuildIdentity> identity;
    std::map<std::string, std::string> environment;
    mode_t file_creation_mask{0022};
};

struct BuildPaths {
    std::filesystem::path recipe_dir;
    std::filesystem::path source_dir;
    std::filesystem::path package_dir;
    std::filesystem::path work_dir;
};

struct DefinitionRequest {
    BuildPaths paths;
    std::optional<std::filesystem::path> config_file;
    ArchiveSpec defaults;
    ExecutionPolicy execution;
};

struct TransformationPolicy {
    bool strip_binaries{true};
    bool compress_manpages{true};
};

struct FootprintPolicy {
    FootprintAction action{FootprintAction::ignore};
    std::optional<std::filesystem::path> manifest;
};

struct BuildRequest {
    DefinitionRequest definition;
    bool download_missing{false};
    bool keep_work{false};
    TransformationPolicy transformations;
    FootprintPolicy footprint;
};

struct DownloadRequest {
    std::string uri;
    std::filesystem::path destination;
};

struct DownloadReceipt {
    std::string uri;
    std::filesystem::path destination;
    std::uintmax_t bytes_written{0};
    bool resumed{false};
};

struct VerificationReceipt {
    std::filesystem::path source;
    Digest expected;
    std::string observed;
};

struct RecipeRequest {
    PackageDefinition definition;
    BuildPaths paths;
    std::filesystem::path source_root;
    std::filesystem::path package_root;
    ExecutionPolicy execution;
};


enum class StagedEntryType {
    regular_file,
    directory,
    symbolic_link,
    fifo,
    character_device,
    block_device,
};

struct StagedTime {
    std::int64_t seconds{0};
    std::uint32_t nanoseconds{0};
};

struct DeviceNumber {
    std::uint64_t major{0};
    std::uint64_t minor{0};
};

struct StagedEntry {
    std::filesystem::path path;
    StagedEntryType type{StagedEntryType::regular_file};
    std::uint32_t mode{0};
    std::uint64_t uid{0};
    std::uint64_t gid{0};
    std::uint64_t size{0};
    StagedTime modification_time;
    std::optional<std::string> symlink_target;
    std::optional<DeviceNumber> device;
    std::optional<std::filesystem::path> hardlink_target;
};

struct StagedPackage {
    std::filesystem::path root;
    std::vector<StagedEntry> entries;
};


struct FootprintEntry {
    std::filesystem::path path;
    StagedEntryType type{StagedEntryType::regular_file};
    std::uint32_t mode{0};
    std::uint64_t uid{0};
    std::uint64_t gid{0};
    std::optional<std::string> symlink_target;
};

struct Footprint {
    std::vector<FootprintEntry> entries;
};

struct FootprintChange {
    FootprintEntry expected;
    FootprintEntry actual;
};

struct FootprintDifference {
    std::vector<FootprintEntry> added;
    std::vector<FootprintEntry> removed;
    std::vector<FootprintChange> changed;

    bool empty() const noexcept
    {
        return added.empty() && removed.empty() && changed.empty();
    }
};

struct FootprintReceipt {
    std::filesystem::path manifest;
    Footprint actual;
    std::optional<Footprint> expected;
    FootprintDifference difference;
    bool written{false};
};

struct PackageWriteRequest {
    StagedPackage package;
    std::filesystem::path output;
    ArchiveSpec archive;
};

struct TransformationChange {
    TransformationKind kind{TransformationKind::strip_binary};
    std::vector<std::filesystem::path> inputs;
    std::vector<std::filesystem::path> outputs;
    std::uintmax_t bytes_before{0};
    std::uintmax_t bytes_after{0};
};

struct TransformationReceipt {
    std::string transformer;
    std::vector<TransformationChange> changes;
};

struct ArchiveReceipt {
    std::filesystem::path output;
    std::uintmax_t bytes_written{0};
    ArchiveSpec archive;
};

struct BuildReceipt {
    PackageDefinition definition;
    std::filesystem::path package;
    std::vector<DownloadReceipt> downloads;
    std::vector<VerificationReceipt> verifications;
    std::vector<TransformationReceipt> transformations;
    std::optional<FootprintReceipt> footprint;
    ArchiveReceipt archive;
    std::optional<std::filesystem::path> work_directory;
};

std::string to_string(ArchiveFormat format);
std::string to_string(Compression compression);
std::string to_string(DigestAlgorithm algorithm);
ArchiveFormat archive_format_from_string(const std::string& value);
Compression compression_from_string(const std::string& value);
std::string package_extension(const ArchiveSpec& archive);
std::filesystem::path package_filename(const PackageDefinition& definition);
Source parse_source(const std::string& declaration);
bool source_is_archive(const std::filesystem::path& path);

} // namespace pkgbuild
