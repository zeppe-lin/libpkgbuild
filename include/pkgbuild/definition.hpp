#pragma once

#include <pkgbuild/types.hpp>

#include <libpkgsource/snapshot.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pkgbuild {

/*! \brief Architecture mode accepted by the build engine. */
enum class BuildArchitecture {
    native,
    legacy_32bit,
};

[[nodiscard]] std::string_view to_string(BuildArchitecture value) noexcept;

/*! \brief Grammar attached to one projected strip exclusion. */
enum class StripPatternSyntax {
    posix_extended_regular_expression,
};

/*! \brief Source-derived strip exclusion in the build-engine contract. */
struct StripExclusion {
    StripPatternSyntax syntax{
        StripPatternSyntax::posix_extended_regular_expression};
    std::string pattern;
};

/*! \brief Caller policy accepted while deriving a build definition.
 *
 * Source facts do not belong here.  This value records the build-engine
 * choices accepted for one realization of an immutable source snapshot.
 */
struct AcceptedBuildPolicy {
    ArchiveSpec archive;
    TransformationPolicy transformations;
    FootprintPolicy footprint;
};

/*! \brief One declared build input projected from source truth.
 *
 * Recipe-local inputs retain their snapshot-bound captured file.  Remote
 * inputs retain only their normalized declaration and locator until the
 * build engine acquires and verifies the corresponding object.
 */
struct BuildSource {
    Source input;
    std::optional<pkgsource::captured_file> captured;
};

/*! \brief Immutable build-engine contract derived from one source snapshot.
 *
 * The definition owns the source snapshot.  This lifetime binding prevents a
 * caller from pairing projected declarations with a different or later
 * package-source observation.
 */
class BuildDefinition final {
public:
    BuildDefinition(const BuildDefinition&) = default;
    BuildDefinition(BuildDefinition&&) noexcept = default;
    BuildDefinition& operator=(const BuildDefinition&) = default;
    BuildDefinition& operator=(BuildDefinition&&) noexcept = default;

    [[nodiscard]] const pkgsource::source_snapshot& snapshot() const noexcept;
    [[nodiscard]] const pkgsource::digest&
    source_snapshot_fingerprint() const noexcept;
    [[nodiscard]] const pkgsource::build_description&
    source_description() const noexcept;
    [[nodiscard]] const PackageId& identity() const noexcept;
    [[nodiscard]] const std::vector<BuildSource>& sources() const noexcept;
    [[nodiscard]] const pkgsource::recipe_descriptor& recipe() const noexcept;
    [[nodiscard]] const std::vector<StripExclusion>&
    strip_exclusions() const noexcept;
    [[nodiscard]] const std::optional<pkgsource::footprint_declaration>&
    footprint() const noexcept;
    [[nodiscard]] BuildArchitecture architecture() const noexcept;
    [[nodiscard]] const AcceptedBuildPolicy& policy() const noexcept;

private:
    friend BuildDefinition derive_definition(
        pkgsource::source_snapshot, AcceptedBuildPolicy);

    BuildDefinition(pkgsource::source_snapshot snapshot,
                    PackageId identity,
                    std::vector<BuildSource> sources,
                    std::vector<StripExclusion> strip_exclusions,
                    std::optional<pkgsource::footprint_declaration> footprint,
                    BuildArchitecture architecture,
                    AcceptedBuildPolicy policy);

    pkgsource::source_snapshot snapshot_;
    PackageId identity_;
    std::vector<BuildSource> sources_;
    std::vector<StripExclusion> strip_exclusions_;
    std::optional<pkgsource::footprint_declaration> footprint_;
    BuildArchitecture architecture_;
    AcceptedBuildPolicy policy_;
};

/*! \brief Exact published-byte evidence issued by the build engine. */
struct SealedArtifactReceipt {
    std::filesystem::path path;
    std::uintmax_t bytes{0};
    Digest digest;
};

/*! \brief Complete result of realizing one immutable build definition. */
struct BuildReceipt {
    BuildDefinition definition;
    std::filesystem::path package;
    std::vector<DownloadReceipt> downloads;
    std::vector<VerificationReceipt> verifications;
    std::vector<TransformationReceipt> transformations;
    std::optional<FootprintReceipt> footprint;
    ArchiveReceipt archive;
    SealedArtifactReceipt artifact;
    std::optional<std::filesystem::path> work_directory;
};

/*! \brief Return the artifact filename selected by accepted policy. */
[[nodiscard]] std::filesystem::path package_filename(
    const BuildDefinition& definition);

/*! \brief Derive one build-engine contract without reopening source paths.
 *
 * \throws Error when the snapshot contains a source fact that this version of
 * libpkgbuild cannot represent or execute.
 */
[[nodiscard]] BuildDefinition derive_definition(
    pkgsource::source_snapshot snapshot,
    AcceptedBuildPolicy policy);

} // namespace pkgbuild
