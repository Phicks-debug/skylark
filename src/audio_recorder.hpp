// Audio recorder header - records from microphone using PortAudio
// Returns recorded audio as WAV file

#ifndef AUDIO_RECORDER_HPP
#define AUDIO_RECORDER_HPP

#include <string>
#include <string_view>
#include <vector>

namespace audio_recorder {

// Record audio from the default microphone
// Records until the user presses Enter (when wait_for_enter is true)
// or for the specified duration in seconds
// Saves as a WAV file to the given path
// Returns true on success
bool record_to_file(std::string_view output_path,
                    bool wait_for_enter = true,
                    int sample_rate = 16000,
                    int channels = 1,
                    int max_duration_sec = 120);

// Record audio and return raw PCM samples
// Returns empty vector on failure
std::vector<int16_t> record_samples(bool wait_for_enter = true,
                                    int sample_rate = 16000,
                                    int channels = 1,
                                    int max_duration_sec = 120);

// Write PCM samples to a WAV file
bool write_wav(std::string_view path,
               const std::vector<int16_t>& samples,
               int sample_rate,
               int channels);

} // namespace audio_recorder

#endif // AUDIO_RECORDER_HPP
