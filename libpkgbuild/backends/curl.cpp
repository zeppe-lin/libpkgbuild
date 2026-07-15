#include <pkgbuild/backends/curl.hpp>
#include <pkgbuild/error.hpp>

#include <curl/curl.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>

namespace pkgbuild {
namespace {

void initialize_curl()
{
    static std::once_flag once;
    static CURLcode result = CURLE_OK;
    std::call_once(once, [] { result = curl_global_init(CURL_GLOBAL_DEFAULT); });
    if (result != CURLE_OK)
        throw Error(ErrorCode::download_failed,
                    "curl global initialization failed");
}

struct FileCloser {
    void operator()(std::FILE* file) const noexcept
    {
        if (file)
            std::fclose(file);
    }
};

size_t write_file(char* data, size_t size, size_t count, void* opaque)
{
    return std::fwrite(data, 1, size * count,
                       static_cast<std::FILE*>(opaque));
}

CURLcode perform(const DownloadRequest& request,
                 const std::filesystem::path& partial,
                 bool resume)
{
    initialize_curl();

    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(
        curl_easy_init(), &curl_easy_cleanup);
    if (!handle)
        throw Error(ErrorCode::download_failed,
                    "curl handle initialization failed");

    const char* mode = resume ? "ab" : "wb";
    std::unique_ptr<std::FILE, FileCloser> output(
        std::fopen(partial.c_str(), mode));
    if (!output)
        throw Error(ErrorCode::download_failed,
                    "cannot open partial download: " + partial.string());

    curl_easy_setopt(handle.get(), CURLOPT_URL, request.uri.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, &write_file);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, output.get());
    curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, "libpkgbuild/0");

    if (resume) {
        const auto offset = std::filesystem::file_size(partial);
        curl_easy_setopt(handle.get(), CURLOPT_RESUME_FROM_LARGE,
                         static_cast<curl_off_t>(offset));
    }

    return curl_easy_perform(handle.get());
}

} // namespace

DownloadReceipt CurlDownloader::fetch(const DownloadRequest& request,
                                      EventSink& events) const
{
    const auto destination = std::filesystem::absolute(request.destination);
    const auto partial = destination.string() + ".partial";
    std::filesystem::create_directories(destination.parent_path());

    bool resumed = std::filesystem::exists(partial);
    emit(events, EventKind::info,
         "Downloading '" + request.uri + "' as '" + destination.string() + "'");

    CURLcode result = CURLE_OK;
    for (int attempt = 0; attempt != 3; ++attempt) {
        result = perform(request, partial, resumed);
        if (result == CURLE_OK)
            break;

        if (resumed) {
            emit(events, EventKind::warning,
                 "Partial download failed; restarting from zero");
            std::filesystem::remove(partial);
            resumed = false;
            continue;
        }
    }

    if (result != CURLE_OK) {
        throw Error(ErrorCode::download_failed,
                    "download failed: " +
                        std::string(curl_easy_strerror(result)));
    }

    std::filesystem::rename(partial, destination);
    return DownloadReceipt{
        request.uri,
        destination,
        std::filesystem::file_size(destination),
        resumed,
    };
}

} // namespace pkgbuild
