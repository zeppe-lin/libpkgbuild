#pragma once

#include <pkgbuild/definition.hpp>
#include <pkgbuild/error.hpp>
#include <pkgbuild/event.hpp>
#include <pkgbuild/source.hpp>
#include <pkgbuild/types.hpp>

#include <string_view>

namespace pkgbuild {

class DefinitionLoader {
public:
    virtual ~DefinitionLoader() = default;
    virtual std::string_view name() const noexcept = 0;
    virtual PackageDefinition load(const DefinitionRequest& request,
                                   EventSink& events) const = 0;
};

class Downloader {
public:
    virtual ~Downloader() = default;
    virtual std::string_view name() const noexcept = 0;
    virtual DownloadReceipt fetch(const DownloadRequest& request,
                                  EventSink& events) const = 0;
};

class SourceVerifier {
public:
    virtual ~SourceVerifier() = default;
    virtual std::string_view name() const noexcept = 0;
    virtual VerifiedSource verify(
        const std::filesystem::path& source,
        const std::vector<Digest>& digests,
        EventSink& events) const = 0;
    virtual void revalidate(const VerifiedSource& source,
                            EventSink& events) const = 0;
};

struct ExtractRequest {
    const VerifiedSource& source;
    std::filesystem::path destination;
};

class SourceExtractor {
public:
    virtual ~SourceExtractor() = default;
    virtual std::string_view name() const noexcept = 0;
    virtual void extract(const ExtractRequest& request,
                         EventSink& events) const = 0;
};

struct CapturedRecipeRequest {
    const BuildDefinition& definition;
    BuildPaths paths;
    std::filesystem::path source_root;
    std::filesystem::path package_root;
    ExecutionPolicy execution;
};

class RecipeRunner {
public:
    virtual ~RecipeRunner() = default;
    virtual std::string_view name() const noexcept = 0;
    virtual StagedPackage run(const RecipeRequest& request,
                              EventSink& events) const = 0;

    virtual StagedPackage run_captured(
        const CapturedRecipeRequest&,
        EventSink&) const
    {
        throw Error(ErrorCode::invalid_configuration,
                    "recipe backend does not support captured execution");
    }
};

struct PackageTransformRequest {
    StagedPackage& package;
    const PackageDefinition& definition;
    const TransformationPolicy& policy;
    const ExecutionPolicy& execution;
};

struct DefinitionTransformRequest {
    StagedPackage& package;
    const BuildDefinition& definition;
    const ExecutionPolicy& execution;
};

class PackageTransformer {
public:
    virtual ~PackageTransformer() = default;
    virtual std::string_view name() const noexcept = 0;
    virtual TransformationReceipt transform(
        const PackageTransformRequest& request,
        EventSink& events) const = 0;

    virtual TransformationReceipt transform_definition(
        const DefinitionTransformRequest&,
        EventSink&) const
    {
        throw Error(
            ErrorCode::invalid_configuration,
            "package transformer does not support build definitions");
    }
};

class NullPackageTransformer final : public PackageTransformer {
public:
    std::string_view name() const noexcept override { return "none"; }
    TransformationReceipt transform(
        const PackageTransformRequest&,
        EventSink&) const override
    {
        return TransformationReceipt{std::string(name()), {}};
    }

    TransformationReceipt transform_definition(
        const DefinitionTransformRequest&,
        EventSink&) const override
    {
        return TransformationReceipt{std::string(name()), {}};
    }
};

class PackageWriter {
public:
    virtual ~PackageWriter() = default;
    virtual std::string_view name() const noexcept = 0;
    virtual bool supports(const ArchiveSpec& archive) const noexcept = 0;
    virtual ArchiveReceipt write(const PackageWriteRequest& request,
                                 EventSink& events) const = 0;
};

} // namespace pkgbuild
