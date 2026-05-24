cmake_minimum_required(VERSION 3.20)
project(GemmaCLI VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# LiteRT-LM shared library
set(LITERT_LM_DYLIB "/Users/phicks/Library/Python/3.9/lib/python/site-packages/litert_lm/liblitert-lm.dylib")

# Homebrew libraries
set(CURL_INCLUDE_DIR "/opt/homebrew/opt/curl/include")
set(CURL_LIB_DIR "/opt/homebrew/opt/curl/lib")
set(PORTAUDIO_INCLUDE_DIR "/opt/homebrew/include")
set(PORTAUDIO_LIB_DIR "/opt/homebrew/lib")
set(NLOHMANN_JSON_DIR "/opt/homebrew/include")

# Find macOS frameworks
find_library(FOUNDATION Foundation REQUIRED)
find_library(COREFOUNDATION CoreFoundation REQUIRED)
find_library(AUDIOTOOLBOX AudioToolbox REQUIRED)
find_library(COREAUDIO CoreAudio REQUIRED)

# Our source files
set(SOURCES
    src/main.cpp
    src/terminal.cpp
    src/model_downloader.cpp
    src/audio_recorder.cpp
    src/tavily_search.cpp
)

# Main executable
add_executable(gemma_cli ${SOURCES})

target_include_directories(gemma_cli PRIVATE
    ${CURL_INCLUDE_DIR}
    ${PORTAUDIO_INCLUDE_DIR}
    ${NLOHMANN_JSON_DIR}
    src/
)

# Link against LiteRT-LM (directly link the dylib)
target_link_libraries(gemma_cli PRIVATE
    "${LITERT_LM_DYLIB}"
    "${CURL_LIB_DIR}/libcurl.dylib"
    "${PORTAUDIO_LIB_DIR}/libportaudio.dylib"
    ${FOUNDATION}
    ${COREFOUNDATION}
    ${AUDIOTOOLBOX}
    ${COREAUDIO}
)

# macOS-specific flags
if(APPLE)
    target_link_options(gemma_cli PRIVATE
        -Wl,-rpath,/Users/phicks/Library/Python/3.9/lib/python/site-packages/litert_lm
        -Wl,-rpath,/opt/homebrew/opt/curl/lib
        -Wl,-rpath,/opt/homebrew/lib
    )
endif()

# Compiler flags
target_compile_options(gemma_cli PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
)
