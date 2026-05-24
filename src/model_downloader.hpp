// Model downloader - downloads model files from HuggingFace Hub
// Uses libcurl with progress reporting

#ifndef MODEL_DOWNLOADER_HPP
#define MODEL_DOWNLOADER_HPP

#include <functional>
#include <string>
#include <string_view>

namespace model_downloader {

// Progress callback: (bytes_downloaded, total_bytes)
using ProgressCallback = std::function<void(size_t, size_t)>;

// Download a file from a URL to a local path, reporting progress
// Returns true on success, false on failure
bool download_file(std::string_view url,
                   std::string_view output_path,
                   ProgressCallback progress = nullptr);

// Check if path is a local file (exists on disk) or a remote HF model ID
bool is_local_path(std::string_view path);

// Get the default cache directory for downloaded models
std::string default_cache_dir();

} // namespace model_downloader

#endif // MODEL_DOWNLOADER_HPP
