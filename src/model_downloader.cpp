// Model downloader implementation using libcurl

#include "model_downloader.hpp"
#include "terminal.hpp"
#include <curl/curl.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace model_downloader {

namespace {

// Callback for writing downloaded data to file
size_t write_callback(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* file = static_cast<std::ofstream*>(userdata);
    size_t total = size * nmemb;
    file->write(static_cast<const char*>(ptr), static_cast<std::streamsize>(total));
    return total;
}

// Progress callback wrapper
struct ProgressData {
    ProgressCallback callback;
    curl_off_t total;
};

int progress_callback(void* clientp,
                      curl_off_t dltotal,
                      curl_off_t dlnow,
                      curl_off_t /*ultotal*/,
                      curl_off_t /*ulnow*/) {
    auto* data = static_cast<ProgressData*>(clientp);
    data->total = dltotal;
    if (data->callback) {
        data->callback(static_cast<size_t>(dlnow),
                       static_cast<size_t>(dltotal));
    }
    return 0;
}

} // namespace

bool download_file(std::string_view url,
                   std::string_view output_path,
                   ProgressCallback progress) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize libcurl\n";
        return false;
    }

    std::ofstream file(std::string(output_path), std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open output file: " << output_path << '\n';
        curl_easy_cleanup(curl);
        return false;
    }

    ProgressData prog_data{progress, 0};

    // Store the URL in a std::string that outlives curl_easy_perform
    std::string url_str(url);
    curl_easy_setopt(curl, CURLOPT_URL, url_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &prog_data);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "GemmaCLI/1.0");

    CURLcode res = curl_easy_perform(curl);
    file.close();
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "Download failed: " << curl_easy_strerror(res) << '\n';
        std::filesystem::remove(output_path);
        return false;
    }

    return true;
}

std::string resolve_model_url(std::string_view model_id) {
    if (is_local_path(model_id)) {
        return std::string(model_id);
    }

    // Construct HF download URL
    // Format: https://huggingface.co/{org}/{model}/resolve/main/{model}.safetensors
    std::string id(model_id);
    std::string url = "https://huggingface.co/" + id + "/resolve/main/";

    // Try to find the model file - common patterns
    // The actual model file name depends on the repo
    // We return the base URL and let the caller handle file discovery
    return url;
}

bool is_local_path(std::string_view path) {
    return fs::exists(path);
}

std::string default_cache_dir() {
    const char* home = std::getenv("HOME");
    if (!home) home = std::getenv("USERPROFILE");
    if (!home) return "./.cache/gemma-cli";

    std::string dir = std::string(home) + "/.cache/gemma-cli/models";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

} // namespace model_downloader
