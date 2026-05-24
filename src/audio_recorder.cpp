// Audio recorder implementation using PortAudio
// Records from default microphone and saves as WAV

#include "audio_recorder.hpp"
#include "terminal.hpp"
#include <portaudio.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <unistd.h>

namespace audio_recorder {

namespace {

// Global flag to signal recording stop (set by Enter key watcher)
std::atomic<bool> g_recording{false};

// Thread-safe sample buffer
std::mutex g_buffer_mutex;
std::vector<int16_t> g_recorded_samples;

// PortAudio callback: captures audio data and appends to global buffer
int record_callback(const void* input,
                    void* /*output*/,
                    unsigned long frame_count,
                    const PaStreamCallbackTimeInfo* /*time_info*/,
                    PaStreamCallbackFlags /*status_flags*/,
                    void* /*user_data*/) {
    if (!input) return paContinue;

    const auto* samples = static_cast<const int16_t*>(input);

    std::lock_guard<std::mutex> lock(g_buffer_mutex);
    g_recorded_samples.insert(g_recorded_samples.end(),
                              samples,
                              samples + frame_count);

    if (!g_recording.load()) {
        return paComplete;
    }
    return paContinue;
}

// WAV file header structure
#pragma pack(push, 1)
struct WavHeader {
    // RIFF header
    char     riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t file_size;   // 4 + (8 + SubChunk1Size) + (8 + SubChunk2Size)
    char     wave[4] = {'W', 'A', 'V', 'E'};

    // fmt subchunk
    char     fmt[4] = {'f', 'm', 't', ' '};
    uint32_t subchunk1_size = 16;  // 16 for PCM
    uint16_t audio_format = 1;     // PCM = 1
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;             // sample_rate * num_channels * bytes_per_sample
    uint16_t block_align;           // num_channels * bytes_per_sample
    uint16_t bits_per_sample = 16;

    // data subchunk
    char     data_header[4] = {'d', 'a', 't', 'a'};
    uint32_t data_size;
};
#pragma pack(pop)

} // namespace

bool record_to_file(std::string_view output_path,
                    bool wait_for_enter,
                    int sample_rate,
                    int channels,
                    int max_duration_sec) {
    auto samples = record_samples(wait_for_enter, sample_rate, channels, max_duration_sec);
    if (samples.empty()) {
        return false;
    }
    return write_wav(output_path, samples, sample_rate, channels);
}

std::vector<int16_t> record_samples(bool wait_for_enter,
                                    int sample_rate,
                                    int channels,
                                    int max_duration_sec) {
    // Initialize PortAudio
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        terminal::cprintln(std::string("PortAudio init failed: ") + Pa_GetErrorText(err),
                          terminal::Color::Red);
        return {};
    }

    // Find the default input device
    PaDeviceIndex input_device = Pa_GetDefaultInputDevice();
    if (input_device == paNoDevice) {
        terminal::cprintln("No audio input device found!", terminal::Color::Red);
        Pa_Terminate();
        return {};
    }

    const PaDeviceInfo* device_info = Pa_GetDeviceInfo(input_device);
    terminal::cprintln(
        std::string("🎤 Recording from: ") + device_info->name,
        terminal::Color::Cyan);

    // Clear global buffer
    {
        std::lock_guard<std::mutex> lock(g_buffer_mutex);
        g_recorded_samples.clear();
    }
    g_recording.store(true);

    // Open the stream
    PaStream* stream = nullptr;
    PaStreamParameters input_params{};
    input_params.device = input_device;
    input_params.channelCount = channels;
    input_params.sampleFormat = paInt16;
    input_params.suggestedLatency = device_info->defaultLowInputLatency;
    input_params.hostApiSpecificStreamInfo = nullptr;

    err = Pa_OpenStream(&stream,
                        &input_params,
                        nullptr,   // no output
                        static_cast<double>(sample_rate),
                        256,       // frames per buffer
                        paClipOff,
                        record_callback,
                        nullptr);

    if (err != paNoError) {
        terminal::cprintln(std::string("Failed to open audio stream: ") + Pa_GetErrorText(err),
                          terminal::Color::Red);
        Pa_Terminate();
        return {};
    }

    // Start recording
    err = Pa_StartStream(stream);
    if (err != paNoError) {
        terminal::cprintln(std::string("Failed to start recording: ") + Pa_GetErrorText(err),
                          terminal::Color::Red);
        Pa_CloseStream(stream);
        Pa_Terminate();
        return {};
    }

    if (wait_for_enter) {
        terminal::cprintln("🔴 Recording... Press ENTER to stop.",
                          terminal::Color::BrightYellow);

        auto start = std::chrono::steady_clock::now();
        while (g_recording.load()) {
            // Check for Enter keypress via poll() on stdin (non-blocking with timeout)
            struct pollfd pfd;
            pfd.fd = STDIN_FILENO;
            pfd.events = POLLIN;
            if (poll(&pfd, 1, 100) > 0) {
                // Data available on stdin — consume the Enter key
                std::cin.get();
                g_recording.store(false);
                break;
            }

            // Check max duration
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count()
                >= max_duration_sec) {
                terminal::cprintln("\n⏰ Max recording time reached.",
                                  terminal::Color::Yellow);
                g_recording.store(false);
                break;
            }
        }
    } else {
        // Record for a fixed duration
        terminal::cprintln(std::string("🔴 Recording for ") +
                          std::to_string(max_duration_sec) + " seconds...",
                          terminal::Color::BrightYellow);
        Pa_Sleep(static_cast<long>(max_duration_sec) * 1000);
        g_recording.store(false);
    }

    // Stop and close the stream
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    // Copy the recorded samples
    std::vector<int16_t> result;
    {
        std::lock_guard<std::mutex> lock(g_buffer_mutex);
        result = std::move(g_recorded_samples);
    }

    if (result.empty()) {
        terminal::cprintln("⚠️  No audio recorded!", terminal::Color::Yellow);
        return {};
    }

    double duration = static_cast<double>(result.size()) /
                      static_cast<double>(sample_rate * channels);
    terminal::cprintln(
        std::string("✅ Recorded ") + std::to_string(duration).substr(0, 4) +
        "s of audio (" + std::to_string(result.size()) + " samples)",
        terminal::Color::Green);

    return result;
}

bool write_wav(std::string_view path,
               const std::vector<int16_t>& samples,
               int sample_rate,
               int channels) {
    std::ofstream file(std::string(path), std::ios::binary);
    if (!file.is_open()) {
        terminal::cprintln("Failed to open WAV file for writing: " + std::string(path),
                          terminal::Color::Red);
        return false;
    }

    WavHeader header;
    header.num_channels = static_cast<uint16_t>(channels);
    header.sample_rate = static_cast<uint32_t>(sample_rate);
    int bytes_per_sample = 2;  // 16-bit
    header.byte_rate = static_cast<uint32_t>(sample_rate * channels * bytes_per_sample);
    header.block_align = static_cast<uint16_t>(channels * bytes_per_sample);
    header.data_size = static_cast<uint32_t>(samples.size() * bytes_per_sample);
    header.file_size = 4 + (8 + header.subchunk1_size) + (8 + header.data_size);

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(samples.data()),
               static_cast<std::streamsize>(samples.size() * bytes_per_sample));
    file.close();

    if (file.fail()) {
        terminal::cprintln("Failed to write WAV data to file: " + std::string(path),
                          terminal::Color::Red);
        return false;
    }
    return true;
}

} // namespace audio_recorder
