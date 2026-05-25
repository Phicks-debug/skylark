cmake_minimum_required(VERSION 3.20)
project(tinyhabibi VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# Find Python and litert_lm package
# ============================================================

# litert_lm package directory (can be overridden via -DLITERT_LM_DIR=...)
# On macOS: ~/Library/Python/X.Y/lib/python/site-packages/litert_lm
# On Linux: ~/.local/lib/pythonX.Y/site-packages/litert_lm or venv/site-packages/litert_lm
set(LITERT_LM_DIR "$ENV{LITERT_LM_DIR}" CACHE PATH "Path to litert_lm package directory (contains liblitert-lm.so/dylib)")

if(NOT LITERT_LM_DIR)
    # Try to find litert_lm using pip first (more reliable across Python versions)
    find_program(PIP_EXECUTABLE pip3)
    if(NOT PIP_EXECUTABLE)
        find_program(PIP_EXECUTABLE pip)
    endif()
    if(PIP_EXECUTABLE)
        execute_process(
            COMMAND ${PIP_EXECUTABLE} show litert-lm
            OUTPUT_VARIABLE _pip_show
            ERROR_QUIET
        )
        if(_pip_show MATCHES "Location: ([^\n]+)")
            set(_pip_location "${CMAKE_MATCH_1}/litert_lm")
            if(EXISTS "${_pip_location}")
                set(LITERT_LM_DIR "${_pip_location}")
            endif()
        endif()
    endif()

    # Fallback: try Python-based detection with multiple Python versions
    if(NOT LITERT_LM_DIR)
        find_package(Python3 QUIET COMPONENTS Interpreter)
        if(NOT Python3_FOUND)
            message(FATAL_ERROR "Python3 not found. Install Python 3.9+ first.")
        endif()
        execute_process(
            COMMAND ${Python3_EXECUTABLE} -c "import litert_lm, os; print(os.path.dirname(litert_lm.__file__))"
            OUTPUT_VARIABLE LITERT_LM_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_VARIABLE _py_err
        )
        if(_py_err OR LITERT_LM_DIR STREQUAL "")
            # Try common Python versions that may have litert_lm installed
            foreach(_py_ver 3.12 3.11 3.10 3.9)
                find_program(_py_exec python${_py_ver})
                if(_py_exec)
                    execute_process(
                        COMMAND ${_py_exec} -c "import litert_lm, os; print(os.path.dirname(litert_lm.__file__))"
                        OUTPUT_VARIABLE LITERT_LM_DIR
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET
                    )
                    if(NOT LITERT_LM_DIR STREQUAL "")
                        message(STATUS "Found litert_lm via python${_py_ver}")
                        break()
                    endif()
                endif()
            endforeach()
        endif()
    endif()

    if(NOT LITERT_LM_DIR OR LITERT_LM_DIR STREQUAL "")
        message(FATAL_ERROR "Failed to locate litert_lm package.\n"
            "Install with: pip install litert-lm\n"
            "Or specify manually: cmake -DLITERT_LM_DIR=/path/to/litert_lm")
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
# SQLite3: prefer Homebrew path on macOS, fall back to system
find_path(SQLITE3_INCLUDE_DIR sqlite3.h
    PATHS /opt/homebrew/opt/sqlite/include /usr/local/opt/sqlite/include
          /opt/homebrew/include /usr/local/include /usr/include)
find_library(SQLITE3_LIBRARY NAMES sqlite3
    PATHS /opt/homebrew/opt/sqlite/lib /usr/local/opt/sqlite/lib
          /opt/homebrew/lib /usr/local/lib /usr/lib)
if(NOT SQLITE3_INCLUDE_DIR OR NOT SQLITE3_LIBRARY)
    message(FATAL_ERROR "SQLite3 not found. Install with: brew install sqlite")
endif()
message(STATUS "Found SQLite3: ${SQLITE3_LIBRARY}")

if(APPLE)
    find_path(CURL_INCLUDE_DIRS curl/curl.h PATHS /opt/homebrew/opt/curl/include /usr/local/opt/curl/include)
    find_library(CURL_LIBRARIES NAMES curl PATHS /opt/homebrew/opt/curl/lib /usr/local/opt/curl/lib)
    find_path(PORTAUDIO_INCLUDE_DIRS portaudio.h PATHS /opt/homebrew/include /usr/local/include)
    find_library(PORTAUDIO_LIBRARIES NAMES portaudio PATHS /opt/homebrew/lib /usr/local/lib)
else()
    # Linux: use pkg-config or fallback to common paths
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(CURL REQUIRED libcurl)
        pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
    endif()
    if(NOT CURL_FOUND OR NOT CURL_INCLUDE_DIRS)
        find_path(CURL_INCLUDE_DIRS curl/curl.h PATHS /usr/include /usr/include/curl /usr/local/include)
        find_library(CURL_LIBRARIES NAMES curl PATHS /usr/lib /usr/local/lib NO_DEFAULT_PATH)
    endif()
    if(NOT PORTAUDIO_FOUND OR NOT PORTAUDIO_INCLUDE_DIRS)
        find_path(PORTAUDIO_INCLUDE_DIRS portaudio.h PATHS /usr/include /usr/local/include)
        find_library(PORTAUDIO_LIBRARIES NAMES portaudio PATHS /usr/lib /usr/local/lib NO_DEFAULT_PATH)
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
    src/bash_tool.cpp
    src/markdown_renderer.cpp
    src/conversation_db.cpp
)

# ============================================================
# Main executable
# ============================================================
add_executable(bb ${SOURCES})

target_include_directories(bb PRIVATE
    ${CURL_INCLUDE_DIRS}
    ${PORTAUDIO_INCLUDE_DIRS}
    ${LITERT_LM_DIR}
    src/
)

# Use -L flag instead of target_link_directories to prevent CMake
# from automatically adding the litert_lm directory to the RPATH.
# The library is bundled alongside the binary at build time.
target_link_options(bb PRIVATE "-L${LITERT_LM_DIR}")
target_link_libraries(bb PRIVATE
    litert-lm
    "${CURL_LIBRARIES}"
    "${PORTAUDIO_LIBRARIES}"
    ${FRAMEWORKS}
)

# SQLite3: link with explicit paths (avoids find_package compatibility issues)
target_include_directories(bb PRIVATE ${SQLITE3_INCLUDE_DIR})
target_link_libraries(bb PRIVATE ${SQLITE3_LIBRARY})

# ============================================================
# RPATH + linker flags (OS-specific)
#
# The litert-lm library is bundled alongside the binary so the
# pre-built release tarball works without requiring litert-lm
# to be at the exact same path as the CI machine.
# ============================================================
if(APPLE)
    get_filename_component(CURL_LIB_DIR "${CURL_LIBRARIES}" DIRECTORY)
    get_filename_component(PORTAUDIO_LIB_DIR "${PORTAUDIO_LIBRARIES}" DIRECTORY)
    target_link_options(bb PRIVATE
        -Wl,-rpath,@executable_path
        -Wl,-rpath,"${CURL_LIB_DIR}"
        -Wl,-rpath,"${PORTAUDIO_LIB_DIR}"
    )

    # Bundle liblitert-lm.dylib next to the binary so it's self-contained.
    # Also create a .so symlink — the dylib's install_name is @rpath/liblitert-lm.so.
    add_custom_command(TARGET bb POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LITERT_LM_DIR}/liblitert-lm.dylib"
            "$<TARGET_FILE_DIR:bb>/liblitert-lm.dylib"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "liblitert-lm.dylib"
            "$<TARGET_FILE_DIR:bb>/liblitert-lm.so"
        COMMENT "Bundling liblitert-lm.dylib next to bb"
    )
else()
    target_link_options(bb PRIVATE
        -Wl,-rpath,'$ORIGIN'
        -Wl,--disable-new-dtags
    )
    target_compile_definitions(bb PRIVATE _GNU_SOURCE)

    # Bundle liblitert-lm.so next to the binary
    add_custom_command(TARGET bb POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LITERT_LM_DIR}/liblitert-lm.so"
            "$<TARGET_FILE_DIR:bb>/liblitert-lm.so"
        COMMENT "Bundling liblitert-lm.so next to bb"
    )
endif()

# ============================================================
# Compiler flags
# ============================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "(GNU|Clang)")
    target_compile_options(bb PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
elseif(MSVC)
    target_compile_options(bb PRIVATE /W4 /WX-)
endif()

# ============================================================
# Installation
# ============================================================
install(TARGETS bb DESTINATION bin)

# Print summary
message(STATUS "==========================================")
message(STATUS "tiny-habibi Build Configuration")
message(STATUS "==========================================")
message(STATUS "  LitertLM lib : ${LITERT_LM_LIB}")
message(STATUS "  LibCurl      : ${CURL_LIBRARIES}")
message(STATUS "  PortAudio    : ${PORTAUDIO_LIBRARIES}")
message(STATUS "  Install path : ${CMAKE_INSTALL_PREFIX}/bin")
message(STATUS "==========================================")
