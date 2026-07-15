#pragma once

#include <pkgbuild/backend.hpp>

namespace pkgbuild {

struct Services {
    DefinitionLoader& definitions;
    Downloader& downloader;
    SourceExtractor& extractor;
    RecipeRunner& recipes;
    PackageWriter& packages;
};

class Engine final {
public:
    explicit Engine(Services services) : services_(services) {}

    PackageDefinition inspect(const DefinitionRequest& request,
                              EventSink& events) const;

    BuildReceipt build(const BuildRequest& request,
                       EventSink& events) const;

private:
    Services services_;
};

} // namespace pkgbuild
