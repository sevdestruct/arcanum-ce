include_guard(GLOBAL)

option(BINK_COMPAT_BOOTSTRAP_FFMPEG
    "Automatically prepare FFmpeg when no local SDK is configured"
    ON)

option(BINK_COMPAT_ALLOW_HOMEBREW_FFMPEG
    "Allow macOS builds to use Homebrew's FFmpeg formula as a developer convenience"
    ON)

set(BINK_COMPAT_FFMPEG_SDK_ROOT "" CACHE PATH "Preferred pinned FFmpeg SDK root")
set(BINK_COMPAT_FFMPEG_HINT_ROOT "" CACHE PATH "Resolved FFmpeg SDK root for bink_compat")
set(BINK_COMPAT_FFMPEG_SOURCE "" CACHE STRING "How the current FFmpeg SDK root was resolved")

function(bink_compat_prepare_ffmpeg)
    set(ffmpeg_root "")
    set(ffmpeg_source "")

    if(DEFINED ENV{FFMPEG_ROOT} AND EXISTS "$ENV{FFMPEG_ROOT}")
        set(ffmpeg_root "$ENV{FFMPEG_ROOT}")
        set(ffmpeg_source "env")
    elseif(DEFINED ENV{FFMPEG_DIR} AND EXISTS "$ENV{FFMPEG_DIR}")
        set(ffmpeg_root "$ENV{FFMPEG_DIR}")
        set(ffmpeg_source "env")
    elseif(BINK_COMPAT_FFMPEG_SDK_ROOT AND EXISTS "${BINK_COMPAT_FFMPEG_SDK_ROOT}")
        set(ffmpeg_root "${BINK_COMPAT_FFMPEG_SDK_ROOT}")
        set(ffmpeg_source "sdk")
    elseif(BINK_COMPAT_FFMPEG_HINT_ROOT
        AND EXISTS "${BINK_COMPAT_FFMPEG_HINT_ROOT}"
        AND NOT (APPLE
            AND NOT IOS
            AND BINK_COMPAT_BOOTSTRAP_FFMPEG)
        AND NOT (BINK_COMPAT_FFMPEG_SOURCE STREQUAL "homebrew"
            AND APPLE
            AND NOT IOS
            AND NOT BINK_COMPAT_ALLOW_HOMEBREW_FFMPEG))
        set(ffmpeg_root "${BINK_COMPAT_FFMPEG_HINT_ROOT}")
        set(ffmpeg_source "hint")
    endif()

    if(NOT ffmpeg_root
        AND BINK_COMPAT_BOOTSTRAP_FFMPEG
        AND WIN32
        AND CMAKE_SIZEOF_VOID_P EQUAL 8
        AND (CMAKE_GENERATOR_PLATFORM STREQUAL "x64"
            OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|x64)$"))
        set(ffmpeg_cache_root "${CMAKE_BINARY_DIR}/_deps/ffmpeg")
        set(ffmpeg_archive_path "${CMAKE_BINARY_DIR}/_deps/ffmpeg-8.0.1-full_build-shared.7z")
        set(ffmpeg_extract_root "${CMAKE_BINARY_DIR}/_deps/ffmpeg-extract")

        if(NOT EXISTS "${ffmpeg_cache_root}/include/libavcodec/avcodec.h"
            OR NOT EXISTS "${ffmpeg_cache_root}/lib/avcodec.lib")
            file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/_deps")
            message(STATUS "bink_compat: fetching pinned FFmpeg SDK for Windows x64")
            file(DOWNLOAD
                "https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-8.0.1-full_build-shared.7z"
                "${ffmpeg_archive_path}"
                EXPECTED_HASH "SHA256=8030dc469fbde247b84cfc21a5c421f3965ffe779bc35de08d78966e0c4a272c"
                SHOW_PROGRESS
                STATUS ffmpeg_download_status)
            list(GET ffmpeg_download_status 0 ffmpeg_download_code)
            list(GET ffmpeg_download_status 1 ffmpeg_download_message)
            if(NOT ffmpeg_download_code EQUAL 0)
                message(FATAL_ERROR "bink_compat: failed to download FFmpeg SDK: ${ffmpeg_download_message}")
            endif()

            file(REMOVE_RECURSE "${ffmpeg_extract_root}")
            file(MAKE_DIRECTORY "${ffmpeg_extract_root}")

            execute_process(
                COMMAND "${CMAKE_COMMAND}" -E tar xvf "${ffmpeg_archive_path}"
                WORKING_DIRECTORY "${ffmpeg_extract_root}"
                RESULT_VARIABLE ffmpeg_extract_result
            )
            if(NOT ffmpeg_extract_result EQUAL 0)
                message(FATAL_ERROR "bink_compat: failed to extract FFmpeg SDK archive")
            endif()

            file(GLOB ffmpeg_extracted_paths LIST_DIRECTORIES TRUE "${ffmpeg_extract_root}/*")
            set(ffmpeg_extracted_root "")
            foreach(ffmpeg_extracted_path IN LISTS ffmpeg_extracted_paths)
                if(EXISTS "${ffmpeg_extracted_path}/include/libavcodec/avcodec.h"
                    AND EXISTS "${ffmpeg_extracted_path}/lib/avcodec.lib")
                    set(ffmpeg_extracted_root "${ffmpeg_extracted_path}")
                    break()
                endif()
            endforeach()

            if(NOT ffmpeg_extracted_root)
                message(FATAL_ERROR "bink_compat: extracted FFmpeg SDK layout was not recognized")
            endif()

            file(REMOVE_RECURSE "${ffmpeg_cache_root}")
            file(RENAME "${ffmpeg_extracted_root}" "${ffmpeg_cache_root}")
            file(REMOVE_RECURSE "${ffmpeg_extract_root}")
        endif()

        set(ffmpeg_root "${ffmpeg_cache_root}")
        set(ffmpeg_source "windows-bootstrap")
    endif()

    if(NOT ffmpeg_root
        AND BINK_COMPAT_BOOTSTRAP_FFMPEG
        AND APPLE
        AND NOT IOS)
        set(ffmpeg_macos_arch "")
        if(CMAKE_OSX_ARCHITECTURES)
            if(CMAKE_OSX_ARCHITECTURES MATCHES ";")
                message(FATAL_ERROR "bink_compat: the prepared FFmpeg SDK is single-architecture; use a native-architecture macOS build or provide FFMPEG_ROOT to a universal FFmpeg SDK")
            endif()
            set(ffmpeg_macos_arch "${CMAKE_OSX_ARCHITECTURES}")
        elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
            set(ffmpeg_macos_arch "arm64")
        elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|x64)$")
            set(ffmpeg_macos_arch "x86_64")
        endif()

        if(ffmpeg_macos_arch)
            set(ffmpeg_macos_sdk_root "${CMAKE_BINARY_DIR}/_deps/ffmpeg-macos-${ffmpeg_macos_arch}")
            set(ffmpeg_macos_deployment_target "${CMAKE_OSX_DEPLOYMENT_TARGET}")
            if(ffmpeg_macos_arch STREQUAL "arm64"
                AND ffmpeg_macos_deployment_target
                AND ffmpeg_macos_deployment_target VERSION_LESS "11.0")
                set(ffmpeg_macos_deployment_target "11.0")
            endif()
            set(ffmpeg_macos_bootstrap_command
                /usr/bin/env
                bash
                "${CMAKE_SOURCE_DIR}/scripts/build_ffmpeg_macos_sdk.sh"
                "${ffmpeg_macos_sdk_root}"
                "${ffmpeg_macos_arch}")
            if(ffmpeg_macos_deployment_target)
                list(APPEND ffmpeg_macos_bootstrap_command "${ffmpeg_macos_deployment_target}")
            endif()

            execute_process(
                COMMAND ${ffmpeg_macos_bootstrap_command}
                RESULT_VARIABLE ffmpeg_macos_bootstrap_result
            )

            if(ffmpeg_macos_bootstrap_result EQUAL 0
                AND EXISTS "${ffmpeg_macos_sdk_root}/include/libavcodec/avcodec.h"
                AND EXISTS "${ffmpeg_macos_sdk_root}/lib/libavcodec.dylib")
                set(ffmpeg_root "${ffmpeg_macos_sdk_root}")
                set(ffmpeg_source "macos-bootstrap")
            elseif(BINK_COMPAT_ALLOW_HOMEBREW_FFMPEG)
                message(WARNING "bink_compat: failed to bootstrap pinned macOS FFmpeg SDK; falling back to Homebrew")
            else()
                message(FATAL_ERROR "bink_compat: failed to bootstrap pinned macOS FFmpeg SDK")
            endif()
        endif()
    endif()

    if(NOT ffmpeg_root
        AND BINK_COMPAT_BOOTSTRAP_FFMPEG
        AND APPLE
        AND NOT IOS
        AND BINK_COMPAT_ALLOW_HOMEBREW_FFMPEG)
        find_program(HOMEBREW_EXECUTABLE brew)
        if(HOMEBREW_EXECUTABLE)
            execute_process(
                COMMAND "${HOMEBREW_EXECUTABLE}" --prefix ffmpeg
                OUTPUT_VARIABLE ffmpeg_brew_prefix
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE ffmpeg_brew_result
                ERROR_QUIET
            )

            if(NOT ffmpeg_brew_result EQUAL 0)
                message(STATUS "bink_compat: installing FFmpeg via Homebrew")
                execute_process(
                    COMMAND "${CMAKE_COMMAND}" -E env HOMEBREW_NO_AUTO_UPDATE=1 "${HOMEBREW_EXECUTABLE}" install ffmpeg pkg-config
                    RESULT_VARIABLE ffmpeg_brew_install_result
                )
                if(NOT ffmpeg_brew_install_result EQUAL 0)
                    message(FATAL_ERROR "bink_compat: failed to install FFmpeg via Homebrew")
                endif()

                execute_process(
                    COMMAND "${HOMEBREW_EXECUTABLE}" --prefix ffmpeg
                    OUTPUT_VARIABLE ffmpeg_brew_prefix
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE ffmpeg_brew_result
                    ERROR_QUIET
                )
            endif()

            if(ffmpeg_brew_result EQUAL 0 AND EXISTS "${ffmpeg_brew_prefix}")
                set(ffmpeg_root "${ffmpeg_brew_prefix}")
                set(ffmpeg_source "homebrew")
            endif()
        endif()
    endif()

    if(NOT ffmpeg_root AND APPLE AND NOT IOS AND BINK_COMPAT_ALLOW_HOMEBREW_FFMPEG)
        if(EXISTS "/opt/homebrew/opt/ffmpeg")
            set(ffmpeg_root "/opt/homebrew/opt/ffmpeg")
            set(ffmpeg_source "homebrew")
        elseif(EXISTS "/usr/local/opt/ffmpeg")
            set(ffmpeg_root "/usr/local/opt/ffmpeg")
            set(ffmpeg_source "homebrew")
        endif()
    endif()

    if(ffmpeg_root
        AND APPLE
        AND NOT IOS
        AND DEFINED CMAKE_OSX_ARCHITECTURES
        AND CMAKE_OSX_ARCHITECTURES MATCHES ";")
        message(FATAL_ERROR "bink_compat: the prepared FFmpeg SDK is single-architecture; use a native-architecture macOS build or provide FFMPEG_ROOT to a universal FFmpeg SDK")
    endif()

    set(BINK_COMPAT_FFMPEG_HINT_ROOT "${ffmpeg_root}" CACHE PATH "Resolved FFmpeg SDK root for bink_compat" FORCE)
    set(BINK_COMPAT_FFMPEG_SOURCE "${ffmpeg_source}" CACHE STRING "How the current FFmpeg SDK root was resolved" FORCE)
    set(BINK_COMPAT_FFMPEG_HINT_ROOT "${ffmpeg_root}" PARENT_SCOPE)
    set(BINK_COMPAT_FFMPEG_SOURCE "${ffmpeg_source}" PARENT_SCOPE)
endfunction()
