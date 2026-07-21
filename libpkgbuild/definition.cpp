#include <pkgbuild/definition.hpp>
#include <pkgbuild/error.hpp>

#include <utility>

namespace pkgbuild {
namespace {

DigestAlgorithm project_digest_algorithm(pkgsource::digest_algorithm value)
{
    switch (value) {
    case pkgsource::digest_algorithm::md5:
        return DigestAlgorithm::md5;
    case pkgsource::digest_algorithm::sha256:
        return DigestAlgorithm::sha256;
    }
    throw Error(ErrorCode::invalid_definition,
                "unsupported source digest algorithm");
}

Digest project_digest(const pkgsource::digest& value)
{
    return Digest{project_digest_algorithm(value.algorithm()), value.hex()};
}

BuildSource project_source(const pkgsource::source_input& value)
{
    Source declaration;
    declaration.declaration = value.declaration();
    declaration.local_name = value.local_name();
    declaration.uri = value.locator();
    declaration.digests.reserve(value.digests().size());
    for (const auto& digest : value.digests())
        declaration.digests.push_back(project_digest(digest));

    switch (value.kind()) {
    case pkgsource::source_input_kind::remote:
        if (!value.locator() || value.local_file())
            throw Error(ErrorCode::invalid_definition,
                        "invalid normalized remote source input: " +
                            value.declaration());
        return BuildSource{std::move(declaration), std::nullopt};

    case pkgsource::source_input_kind::recipe_local:
        if (value.locator() || !value.local_file())
            throw Error(ErrorCode::invalid_definition,
                        "invalid normalized recipe-local source input: " +
                            value.declaration());
        return BuildSource{std::move(declaration), value.local_file()};
    }

    throw Error(ErrorCode::invalid_definition,
                "unsupported normalized source input kind");
}

StripExclusion project_strip_exclusion(
    const pkgsource::strip_exclusion& value)
{
    switch (value.syntax()) {
    case pkgsource::strip_pattern_syntax::posix_extended_regular_expression:
        return StripExclusion{
            StripPatternSyntax::posix_extended_regular_expression,
            value.pattern(),
        };
    }
    throw Error(ErrorCode::invalid_definition,
                "unsupported strip exclusion grammar");
}

BuildArchitecture project_architecture(pkgsource::build_architecture value)
{
    switch (value) {
    case pkgsource::build_architecture::native:
        return BuildArchitecture::native;
    case pkgsource::build_architecture::legacy_32bit:
        return BuildArchitecture::legacy_32bit;
    }
    throw Error(ErrorCode::invalid_definition,
                "unsupported build architecture");
}

void validate_recipe(const pkgsource::recipe_descriptor& recipe)
{
    if (recipe.format() != pkgsource::source_format::pkgfile_v0)
        throw Error(ErrorCode::invalid_definition,
                    "unsupported captured recipe format");
    if (recipe.entry_point() != pkgsource::recipe_entry_point::build)
        throw Error(ErrorCode::invalid_definition,
                    "unsupported captured recipe entry point");
    if (!recipe.program())
        throw Error(ErrorCode::invalid_definition,
                    "captured recipe program is missing");
}

void validate_footprint(
    const std::optional<pkgsource::footprint_declaration>& footprint)
{
    if (footprint &&
        footprint->format() != pkgsource::footprint_format::pkgfile_footprint_v0)
        throw Error(ErrorCode::invalid_definition,
                    "unsupported captured footprint format");
}

void validate_policy(
    const AcceptedBuildPolicy& policy,
    const std::optional<pkgsource::footprint_declaration>& footprint)
{
    (void)to_string(policy.archive.format);
    (void)to_string(policy.archive.compression);

    switch (policy.footprint.action) {
    case FootprintAction::ignore:
        if (policy.footprint.manifest)
            throw Error(ErrorCode::invalid_configuration,
                        "ignored footprint policy has a manifest path");
        return;

    case FootprintAction::compare:
        if (policy.footprint.manifest)
            throw Error(ErrorCode::invalid_configuration,
                        "snapshot footprint comparison must not reopen a "
                        "manifest path");
        if (!footprint)
            throw Error(ErrorCode::invalid_configuration,
                        "footprint comparison requested without a captured "
                        "source footprint");
        return;

    case FootprintAction::write:
        if (!policy.footprint.manifest ||
            policy.footprint.manifest->empty() ||
            !policy.footprint.manifest->is_absolute())
            throw Error(ErrorCode::invalid_configuration,
                        "footprint write policy requires an explicit absolute "
                        "output path");
        return;
    }

    throw Error(ErrorCode::invalid_configuration,
                "unsupported footprint policy");
}

} // namespace

BuildDefinition::BuildDefinition(
    pkgsource::source_snapshot snapshot,
    PackageId identity,
    std::vector<BuildSource> sources,
    std::vector<StripExclusion> strip_exclusions,
    std::optional<pkgsource::footprint_declaration> footprint,
    BuildArchitecture architecture,
    AcceptedBuildPolicy policy)
    : snapshot_(std::move(snapshot)),
      identity_(std::move(identity)),
      sources_(std::move(sources)),
      strip_exclusions_(std::move(strip_exclusions)),
      footprint_(std::move(footprint)),
      architecture_(architecture),
      policy_(std::move(policy))
{
}

const pkgsource::source_snapshot& BuildDefinition::snapshot() const noexcept
{
    return snapshot_;
}

const pkgsource::digest&
BuildDefinition::source_snapshot_fingerprint() const noexcept
{
    return snapshot_.fingerprint();
}

const pkgsource::build_description&
BuildDefinition::source_description() const noexcept
{
    return snapshot_.build();
}

const PackageId& BuildDefinition::identity() const noexcept
{
    return identity_;
}

const std::vector<BuildSource>& BuildDefinition::sources() const noexcept
{
    return sources_;
}

const pkgsource::recipe_descriptor& BuildDefinition::recipe() const noexcept
{
    return snapshot_.build().recipe();
}

const std::vector<StripExclusion>&
BuildDefinition::strip_exclusions() const noexcept
{
    return strip_exclusions_;
}

const std::optional<pkgsource::footprint_declaration>&
BuildDefinition::footprint() const noexcept
{
    return footprint_;
}

BuildArchitecture BuildDefinition::architecture() const noexcept
{
    return architecture_;
}

const AcceptedBuildPolicy& BuildDefinition::policy() const noexcept
{
    return policy_;
}

BuildDefinition derive_definition(pkgsource::source_snapshot snapshot,
                                  AcceptedBuildPolicy policy)
{
    if (snapshot.format() != pkgsource::source_format::pkgfile_v0)
        throw Error(ErrorCode::invalid_definition,
                    "unsupported package-source format");

    const auto& description = snapshot.build();
    validate_recipe(description.recipe());
    validate_footprint(description.footprint());
    validate_policy(policy, description.footprint());

    const auto& source_identity = description.identity();
    PackageId identity{
        source_identity.name(),
        source_identity.version(),
        source_identity.release(),
    };

    std::vector<BuildSource> sources;
    sources.reserve(description.sources().size());
    for (const auto& source : description.sources())
        sources.push_back(project_source(source));

    std::vector<StripExclusion> strip_exclusions;
    strip_exclusions.reserve(description.strip_exclusions().size());
    for (const auto& exclusion : description.strip_exclusions())
        strip_exclusions.push_back(project_strip_exclusion(exclusion));

    auto footprint = description.footprint();
    const auto architecture = project_architecture(description.architecture());

    return BuildDefinition(
        std::move(snapshot),
        std::move(identity),
        std::move(sources),
        std::move(strip_exclusions),
        std::move(footprint),
        architecture,
        std::move(policy));
}

} // namespace pkgbuild
