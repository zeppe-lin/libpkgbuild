#pragma once

#include <pkgbuild/backend.hpp>
#include <pkgbuild/process.hpp>

namespace pkgbuild {

class FakerootPkgfileRecipeRunner final : public RecipeRunner {
public:
    FakerootPkgfileRecipeRunner(std::filesystem::path fakeroot,
                                std::filesystem::path helper,
                                std::filesystem::path scanner,
                                const ProcessExecutor& processes)
        : fakeroot_(std::move(fakeroot)),
          helper_(std::move(helper)),
          scanner_(std::move(scanner)),
          processes_(processes) {}

    std::string_view name() const noexcept override
    {
        return "pkgfile/0-fakeroot";
    }

    StagedPackage run(const RecipeRequest& request,
                      EventSink& events) const override;
    StagedPackage run_captured(const CapturedRecipeRequest& request,
                               EventSink& events) const override;

private:
    std::filesystem::path fakeroot_;
    std::filesystem::path helper_;
    std::filesystem::path scanner_;
    const ProcessExecutor& processes_;
};

} // namespace pkgbuild
