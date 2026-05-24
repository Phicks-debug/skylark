cmake_minimum_required(VERSION 3.20)
project(GemmaCLI VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# Find Python and litert_lm package
# ============================================================
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# litert_lm package directory (can be overridden via -DLITERT_LM_DIR=...)
# On macOS: ~/Library/Python/X.Y/lib/python/site-packages/litert_lm
# On Linux: ~/.local/lib/pythonX.Y/site-packages/litert_lm or venv/site-packages/litert_lm
set(LITERT_LM_DIR "$ENV{LITERT_LM_DIR}" CACHE PATH "Path to litert_lm package directory (contains liblitert-lm.so/dylib)")

if(NOT LITERT_LM_DIR)
    find_package(Python3 REQUIRED)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c "import litert_lm, os; print(os.path.dirname(litert_lm.__file__))"
        OUTPUT_VARIABLE LITERT_LM_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_VARIABLE _py_err
    )
    if(_py_err OR LITERT_LM_DIR STREQUAL "")
        message(FATAL_ERROR "Failed to locate litert_lm package.\n"
            "Python error: ${_py_err}\n"
            "Install with: pip install litert-lm")
    endif()
endif()

message(STATUS "Found litert_lm at: ${LITERT_LM_DIR}")

# ============================================================
# Detect OS and set library suffix + framework flags
# ============================================================
if(APPLE)
    set(LITERT_LM_LIB "${LITERT_LM_DIR}/liblitert-lm.dylib")
    set(LIB_SUFFIX ".dylib")
    set(LIBCURL_LIB "libcurl.dylib")
    set(LIBPORTAUDIO_LIB "libportaudio.dylib")
    set(RPATH_PREFIX "@executable_path/../Frameworks")

    # Find macOS frameworks
    find_library(FOUNDATION Foundation REQUIRED)
    find_library(COREFOUNDATION CoreFoundation REQUIRED)
    find_library(AUDIOTOOLBOX AudioToolbox REQUIRED)
    find_library(COREAUDIO CoreAudio REQUIRED)
    set(FRAMEWORKS ${FOUNDATION} ${COREFOUNDATION} ${AUDIOTOOLBOX} ${COREAUDIO})
else()
    set(LITERT_LM_LIB "${LITERT_LM_DIR}/liblitert-lm.so")
    set(LIB_SUFFIX ".so")
    set(LIBCURL_LIB "libcurl.so")
    set(LIBPORTAUDIO_LIB "libportaudio.so")
    set(RPATH_PREFIX "$ORIGIN/../lib")
    set(FRAMEWORKS "")
endif()

# Verify the library exists
if(NOT EXISTS "${LITERT_LM_LIB}")
    message(FATAL_ERROR "LiteRT-LM library not found at: ${LITERT_LM_LIB}\n"
        "Install with: pip install litert-lm")
endif()

# ============================================================
# System dependencies (pkg-config or standard paths)
# ============================================================
if(APPLE)
    set(CURL_INCLUDE_DIR "/opt/homebrew/opt/curl/include")
    set(CURL_LIB_DIR "/opt/homebrew/opt/curl/lib")
    set(PORTAUDIO_INCLUDE_DIR "/opt/homebrew/include")
    set(PORTAUDIO_LIB_DIR "/opt/homebrew/lib")
else()
    # Linux: use pkg-config or fallback to common paths
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(CURL REQUIRED libcurl)
        pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
    endif()
    if(NOT CURL_FOUND OR NOT CURL_INCLUDE_DIRS)
        find_path(CURL_INCLUDE_DIR curl/curl.h PATHS /usr/include /usr/include/curl /usr/local/include)
        find_library(CURL_LIB NAMES curl PATHS /usr/lib /usr/local/lib NO_DEFAULT_PATH)
    endif()
    if(NOT PORTAUDIO_FOUND OR NOT PORTAUDIO_INCLUDE_DIRS)
        find_path(PORTAUDIO_INCLUDE_DIR portaudio.h PATHS /usr/include /usr/local/include)
        find_library(PORTAUDIO_LIB NAMES portaudio PATHS /usr/lib /usr/local/lib NO_DEFAULT_PATH)
    endif()
endif()

# ============================================================
# Our source files
# ============================================================
set(SOURCES
    src/main.cpp
    src/terminal.cpp
    src/model_downloader.cpp
    src/audio_recorder.cpp
    src/tavily_search.cpp
)

# ============================================================
# Main executable
# ============================================================
add_executable(gemma_cli ${SOURCES})

target_include_directories(gemma_cli PRIVATE
    ${CURL_INCLUDE_DIRS}
    ${PORTAUDIO_INCLUDE_DIRS}
    ${LITERT_LM_DIR}
    src/
)

target_link_libraries(gemma_cli PRIVATE
    "${LITERT_LM_LIB}"
    "${CURL_LIBRARIES}"
    "${PORTAUDIO_LIBRARIES}"
    ${FRAMEWORKS}
)

# ============================================================
# RPATH + linker flags (OS-specific)
# ============================================================
if(APPLE)
    target_link_options(gemma_cli PRIVATE
        -Wl,-rpath,"${LITERT_LM_DIR}"
        -Wl,-rpath,"${CURL_LIB_DIR}"
        -Wl,-rpath,"${PORTAUDIO_LIB_DIR}"
    )
else()
    target_link_options(gemma_cli PRIVATE
        -Wl,-rpath,"${LITERT_LM_DIR}"
        -Wl,--disable-new-dtags
    )
    target_compile_definitions(gemma_cli PRIVATE _GNU_SOURCE)
endif()

# ============================================================
# Compiler flags
# ============================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "(GNU|Clang)")
    target_compile_options(gemma_cli PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
elseif(MSVC)
    target_compile_options(gemma_cli PRIVATE /W4 /WX-)
endif()

# ============================================================
# Installation
# ============================================================
install(TARGETS gemma_cli DESTINATION bin)

# Print summary
message(STATUS "==========================================")
message(STATUS "GemmaCLI Build Configuration")
message(STATUS "==========================================")
message(STATUS "  LitertLM lib : ${LITERT_LM_LIB}")
message(STATUS "  LibCurl      : ${CURL_LIB}")
message(STATUS "  PortAudio    : ${PORTAUDIO_LIB}")
message(STATUS "  Install path : ${CMAKE_INSTALL_PREFIX}/bin")
message(STATUS "==========================================")
