#include "archive_compare.hpp"

#include <libpkgimage/entry_selection.h>
#include <libpkgimage/libarchive_backend.h>
#include <libpkgimage/package_archive.h>
#include <libpkgimage/payload_sink.h>

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <iomanip>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace pkgbuild::parity {
namespace {

struct EvpContextDeleter {
    void operator()(EVP_MD_CTX* value) const noexcept
    {
        EVP_MD_CTX_free(value);
    }
};

using EvpContext = std::unique_ptr<EVP_MD_CTX, EvpContextDeleter>;

std::string hex_digest(const unsigned char* data, unsigned int size)
{
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int i = 0; i != size; ++i)
        output << std::setw(2) << static_cast<unsigned int>(data[i]);
    return output.str();
}

class PayloadHasher final : public pkgimage::payload_sink {
public:
    void begin(const pkgimage::package_entry& entry) override
    {
        current_path_ = entry.path.string();
        context_.reset(EVP_MD_CTX_new());
        if (!context_ || EVP_DigestInit_ex(context_.get(), EVP_sha256(), nullptr) != 1)
            throw std::runtime_error("cannot initialize SHA-256 payload digest");
    }

    void write(const pkgimage::package_entry&,
               const std::byte* data,
               std::size_t size) override
    {
        if (EVP_DigestUpdate(context_.get(), data, size) != 1)
            throw std::runtime_error("cannot update SHA-256 payload digest");
    }

    void end(const pkgimage::package_entry&) override
    {
        std::array<unsigned char, EVP_MAX_MD_SIZE> digest {};
        unsigned int size = 0;
        if (EVP_DigestFinal_ex(context_.get(), digest.data(), &size) != 1)
            throw std::runtime_error("cannot finalize SHA-256 payload digest");
        hashes_.emplace(current_path_, hex_digest(digest.data(), size));
        context_.reset();
        current_path_.clear();
    }

    const std::map<std::string, std::string>& hashes() const noexcept
    {
        return hashes_;
    }

private:
    EvpContext context_;
    std::string current_path_;
    std::map<std::string, std::string> hashes_;
};

std::string entry_type_name(pkgimage::entry_type type)
{
    switch (type) {
    case pkgimage::entry_type::regular: return "regular";
    case pkgimage::entry_type::directory: return "directory";
    case pkgimage::entry_type::symlink: return "symlink";
    case pkgimage::entry_type::hardlink: return "regular";
    case pkgimage::entry_type::fifo: return "fifo";
    case pkgimage::entry_type::character_device: return "character-device";
    case pkgimage::entry_type::block_device: return "block-device";
    }
    throw std::runtime_error("unknown package entry type");
}

std::string octal_mode(std::uint32_t mode)
{
    std::ostringstream output;
    output << '0' << std::oct << mode;
    return output.str();
}

std::string device_text(const std::optional<pkgimage::device_number>& device)
{
    if (!device)
        return {};
    return std::to_string(device->major) + ":" +
           std::to_string(device->minor);
}

std::string join_paths(const std::vector<std::string>& paths)
{
    std::ostringstream output;
    for (std::size_t i = 0; i != paths.size(); ++i) {
        if (i != 0)
            output << ',';
        output << paths[i];
    }
    return output.str();
}

struct SemanticEntry {
    std::string type;
    std::uint32_t mode{0};
    std::uint64_t uid{0};
    std::uint64_t gid{0};
    std::uint64_t size{0};
    std::string symlink_target;
    std::string hardlink_group;
    std::string device;
    std::string payload_sha256;
};

using SemanticArchive = std::map<std::string, SemanticEntry>;

std::map<std::string, std::vector<std::string>> hardlink_groups(
    const pkgimage::package_image& image)
{
    std::map<std::string, std::vector<std::string>> by_target;
    for (const auto& entry : image.entries()) {
        if (entry.type == pkgimage::entry_type::regular)
            by_target[entry.path.string()].push_back(entry.path.string());
    }
    for (const auto& entry : image.entries()) {
        if (entry.type != pkgimage::entry_type::hardlink)
            continue;
        by_target[entry.hardlink_target->string()].push_back(entry.path.string());
    }

    std::map<std::string, std::vector<std::string>> by_member;
    for (auto& item : by_target) {
        auto& members = item.second;
        if (members.size() < 2)
            continue;
        std::sort(members.begin(), members.end());
        for (const auto& member : members)
            by_member.emplace(member, members);
    }
    return by_member;
}

SemanticArchive inspect_archive(const std::filesystem::path& path)
{
    pkgimage::libarchive_backend backend;
    auto archive = backend.open(path);
    const auto& image = archive->image();

    PayloadHasher hasher;
    archive->replay(pkgimage::entry_selection::all_regular(image), hasher);

    const auto groups = hardlink_groups(image);
    SemanticArchive result;
    for (const auto& entry : image.entries()) {
        const pkgimage::package_entry* metadata = &entry;
        std::string payload_path = entry.path.string();
        if (entry.type == pkgimage::entry_type::hardlink) {
            metadata = image.find(*entry.hardlink_target);
            if (metadata == nullptr)
                throw std::runtime_error("hardlink target disappeared during inspection");
            payload_path = metadata->path.string();
        }

        SemanticEntry normalized;
        normalized.type = entry_type_name(entry.type);
        normalized.mode = metadata->mode;
        normalized.uid = metadata->uid;
        normalized.gid = metadata->gid;
        normalized.size = metadata->size;
        if (entry.symlink_target)
            normalized.symlink_target = *entry.symlink_target;
        const auto group = groups.find(entry.path.string());
        if (group != groups.end())
            normalized.hardlink_group = join_paths(group->second);
        normalized.device = device_text(metadata->device);
        if (metadata->type == pkgimage::entry_type::regular) {
            const auto hash = hasher.hashes().find(payload_path);
            if (hash == hasher.hashes().end())
                throw std::runtime_error("regular payload hash is absent for '" +
                                         payload_path + "'");
            normalized.payload_sha256 = hash->second;
        }
        result.emplace(entry.path.string(), std::move(normalized));
    }
    return result;
}

void add_difference(ArchiveComparison& report,
                    const std::string& path,
                    const std::string& field,
                    std::string reference,
                    std::string candidate)
{
    report.differences.push_back(
        ArchiveDifference{path, field, std::move(reference),
                          std::move(candidate)});
}

template <typename T>
void compare_field(ArchiveComparison& report,
                   const std::string& path,
                   const std::string& field,
                   const T& reference,
                   const T& candidate)
{
    if (reference == candidate)
        return;
    std::ostringstream left;
    std::ostringstream right;
    left << reference;
    right << candidate;
    add_difference(report, path, field, left.str(), right.str());
}

void compare_entry(ArchiveComparison& report,
                   const std::string& path,
                   const SemanticEntry& reference,
                   const SemanticEntry& candidate)
{
    compare_field(report, path, "type", reference.type, candidate.type);
    if (reference.mode != candidate.mode)
        add_difference(report, path, "mode", octal_mode(reference.mode),
                       octal_mode(candidate.mode));
    compare_field(report, path, "uid", reference.uid, candidate.uid);
    compare_field(report, path, "gid", reference.gid, candidate.gid);
    compare_field(report, path, "size", reference.size, candidate.size);
    compare_field(report, path, "symlink-target", reference.symlink_target,
                  candidate.symlink_target);
    compare_field(report, path, "hardlink-group", reference.hardlink_group,
                  candidate.hardlink_group);
    compare_field(report, path, "device", reference.device, candidate.device);
    compare_field(report, path, "payload-sha256", reference.payload_sha256,
                  candidate.payload_sha256);
}

} // namespace

ArchiveComparison compare_archives(
    const std::filesystem::path& reference,
    const std::filesystem::path& candidate)
{
    ArchiveComparison report{reference, candidate, {}};
    const auto reference_image = inspect_archive(reference);
    const auto candidate_image = inspect_archive(candidate);

    for (const auto& item : reference_image) {
        const auto found = candidate_image.find(item.first);
        if (found == candidate_image.end()) {
            add_difference(report, item.first, "presence", "present", "absent");
            continue;
        }
        compare_entry(report, item.first, item.second, found->second);
    }
    for (const auto& item : candidate_image) {
        if (reference_image.find(item.first) == reference_image.end())
            add_difference(report, item.first, "presence", "absent", "present");
    }
    return report;
}

std::string format_difference(const ArchiveDifference& difference)
{
    return difference.path + ": " + difference.field + ": " +
           difference.reference + " -> " + difference.candidate;
}

} // namespace pkgbuild::parity
