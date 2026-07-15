#pragma once

#include <pkgbuild/event.hpp>
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

class SourceExtractor {
public:
    virtual ~SourceExtractor() = default;
    virtual std::string_view name() const noexcept = 0;
    virtual void extract(const ExtractRequest& request,
                         EventSink& events) const = 0;
};

class RecipeRunner {
public:
    virtual ~RecipeRunner() = default;
    virtual std::string_view name() const noexcept = 0;
    virtual void run(const RecipeRequest& request,
                     EventSink& events) const = 0;
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
