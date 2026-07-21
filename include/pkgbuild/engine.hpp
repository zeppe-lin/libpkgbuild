#pragma once

#include <pkgbuild/backend.hpp>

namespace pkgbuild {

struct BuildServices {
    Downloader& downloader;
    SourceVerifier& verifier;
    SourceExtractor& extractor;
    RecipeRunner& recipes;
    PackageTransformer& transformer;
    PackageWriter& packages;
};

struct Services {
    DefinitionLoader& definitions;
    Downloader& downloader;
    SourceVerifier& verifier;
    SourceExtractor& extractor;
    RecipeRunner& recipes;
    PackageTransformer& transformer;
    PackageWriter& packages;
};

class Engine final {
public:
    explicit Engine(BuildServices services) : services_(services) {}
    explicit Engine(Services services)
        : services_{services.downloader,
                    services.verifier,
                    services.extractor,
                    services.recipes,
                    services.transformer,
                    services.packages},
          legacy_definitions_(&services.definitions)
    {
    }

    PackageDefinition inspect(const DefinitionRequest& request,
                              EventSink& events) const;

    LegacyBuildReceipt build(const BuildRequest& request,
                             EventSink& events) const;

    BuildReceipt build(const BuildDefinition& definition,
                       const BuildEnvironment& environment,
                       EventSink& events) const;

private:
    BuildServices services_;
    DefinitionLoader* legacy_definitions_{nullptr};
};

} // namespace pkgbuild
