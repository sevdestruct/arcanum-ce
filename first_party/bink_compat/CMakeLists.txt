add_library(bink_compat STATIC
    "include/bink_compat.h"
    "src/bink_compat.c"
)

target_include_directories(bink_compat PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

include("${CMAKE_SOURCE_DIR}/cmake/prepare_ffmpeg.cmake")

option(ARCANUM_VIDEO_PLAYBACK "Enable video playback via FFmpeg (opt-in)" OFF)

set(BINK_COMPAT_ENABLE_FFMPEG OFF)
if(ARCANUM_VIDEO_PLAYBACK AND (NOT WIN32 OR CMAKE_SIZEOF_VOID_P EQUAL 8))
    set(BINK_COMPAT_ENABLE_FFMPEG ON)
endif()

set(BINK_COMPAT_FFMPEG_RUNTIME_DIR "")
set(BINK_COMPAT_FFMPEG_SEARCH_DIRS "")
set(BINK_COMPAT_FFMPEG_SOURCE "")

if(BINK_COMPAT_ENABLE_FFMPEG)
    set(BINK_COMPAT_FFMPEG_FOUND OFF)
    bink_compat_prepare_ffmpeg()

    unset(FFMPEG_FOUND CACHE)
    unset(FFMPEG_INCLUDE_DIR CACHE)
    unset(FFMPEG_AVCODEC_LIBRARY CACHE)
    unset(FFMPEG_AVFORMAT_LIBRARY CACHE)
    unset(FFMPEG_AVUTIL_LIBRARY CACHE)
    unset(FFMPEG_SWSCALE_LIBRARY CACHE)
    unset(FFMPEG_SWRESAMPLE_LIBRARY CACHE)

    set(BINK_COMPAT_FFMPEG_FIND_ARGS
        HINTS
            "${BINK_COMPAT_FFMPEG_HINT_ROOT}"
            ENV FFMPEG_ROOT
            ENV FFMPEG_DIR
        PATH_SUFFIXES include)
    set(BINK_COMPAT_FFMPEG_LIBRARY_FIND_ARGS
        HINTS
            "${BINK_COMPAT_FFMPEG_HINT_ROOT}"
            ENV FFMPEG_ROOT
            ENV FFMPEG_DIR
        PATH_SUFFIXES lib)
    if(APPLE AND NOT IOS AND NOT BINK_COMPAT_ALLOW_HOMEBREW_FFMPEG)
        list(APPEND BINK_COMPAT_FFMPEG_FIND_ARGS NO_DEFAULT_PATH)
        list(APPEND BINK_COMPAT_FFMPEG_LIBRARY_FIND_ARGS NO_DEFAULT_PATH)
    endif()

    if((NOT BINK_COMPAT_FFMPEG_HINT_ROOT OR BINK_COMPAT_FFMPEG_SOURCE STREQUAL "homebrew")
        AND NOT (APPLE AND NOT IOS AND NOT BINK_COMPAT_ALLOW_HOMEBREW_FFMPEG))
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET
                libavcodec libavformat libavutil libswscale libswresample)
            if(FFMPEG_FOUND)
                set(BINK_COMPAT_FFMPEG_FOUND ON)
            endif()
        endif()
    endif()

    if(NOT BINK_COMPAT_FFMPEG_FOUND)
        find_path(FFMPEG_INCLUDE_DIR
            NAMES libavcodec/avcodec.h
            ${BINK_COMPAT_FFMPEG_FIND_ARGS}
        )
        find_library(FFMPEG_AVCODEC_LIBRARY
            NAMES avcodec
            ${BINK_COMPAT_FFMPEG_LIBRARY_FIND_ARGS}
        )
        find_library(FFMPEG_AVFORMAT_LIBRARY
            NAMES avformat
            ${BINK_COMPAT_FFMPEG_LIBRARY_FIND_ARGS}
        )
        find_library(FFMPEG_AVUTIL_LIBRARY
            NAMES avutil
            ${BINK_COMPAT_FFMPEG_LIBRARY_FIND_ARGS}
        )
        find_library(FFMPEG_SWSCALE_LIBRARY
            NAMES swscale
            ${BINK_COMPAT_FFMPEG_LIBRARY_FIND_ARGS}
        )
        find_library(FFMPEG_SWRESAMPLE_LIBRARY
            NAMES swresample
            ${BINK_COMPAT_FFMPEG_LIBRARY_FIND_ARGS}
        )

        if(FFMPEG_INCLUDE_DIR
            AND FFMPEG_AVCODEC_LIBRARY
            AND FFMPEG_AVFORMAT_LIBRARY
            AND FFMPEG_AVUTIL_LIBRARY
            AND FFMPEG_SWSCALE_LIBRARY
            AND FFMPEG_SWRESAMPLE_LIBRARY)
            set(BINK_COMPAT_FFMPEG_FOUND ON)
        endif()
    endif()

    if(BINK_COMPAT_FFMPEG_FOUND)
        target_compile_definitions(bink_compat PRIVATE HAVE_FFMPEG)

        if(WIN32)
            if(FFMPEG_FOUND)
                if(DEFINED FFMPEG_INCLUDE_DIRS)
                    target_include_directories(bink_compat PRIVATE ${FFMPEG_INCLUDE_DIRS})
                endif()
                if(DEFINED FFMPEG_LIBRARY_DIRS)
                    list(APPEND BINK_COMPAT_FFMPEG_SEARCH_DIRS ${FFMPEG_LIBRARY_DIRS})
                endif()
            else()
                target_include_directories(bink_compat PRIVATE "${FFMPEG_INCLUDE_DIR}")
            endif()
        else()
            if(FFMPEG_FOUND)
                if(TARGET PkgConfig::FFMPEG)
                    target_link_libraries(bink_compat PUBLIC PkgConfig::FFMPEG)
                else()
                    if(DEFINED FFMPEG_INCLUDE_DIRS)
                        target_include_directories(bink_compat PRIVATE ${FFMPEG_INCLUDE_DIRS})
                    endif()
                    if(DEFINED FFMPEG_LIBRARY_DIRS)
                        target_link_directories(bink_compat PUBLIC ${FFMPEG_LIBRARY_DIRS})
                    endif()
                    if(DEFINED FFMPEG_LINK_LIBRARIES)
                        target_link_libraries(bink_compat PUBLIC ${FFMPEG_LINK_LIBRARIES})
                    elseif(DEFINED FFMPEG_LIBRARIES)
                        target_link_libraries(bink_compat PUBLIC ${FFMPEG_LIBRARIES})
                    endif()
                endif()
                if(DEFINED FFMPEG_LIBRARY_DIRS)
                    list(APPEND BINK_COMPAT_FFMPEG_SEARCH_DIRS ${FFMPEG_LIBRARY_DIRS})
                endif()
            else()
                target_include_directories(bink_compat PRIVATE "${FFMPEG_INCLUDE_DIR}")
                target_link_libraries(bink_compat PUBLIC
                    "${FFMPEG_AVCODEC_LIBRARY}"
                    "${FFMPEG_AVFORMAT_LIBRARY}"
                    "${FFMPEG_AVUTIL_LIBRARY}"
                    "${FFMPEG_SWSCALE_LIBRARY}"
                    "${FFMPEG_SWRESAMPLE_LIBRARY}"
                )
                foreach(ffmpeg_library IN ITEMS
                    "${FFMPEG_AVCODEC_LIBRARY}"
                    "${FFMPEG_AVFORMAT_LIBRARY}"
                    "${FFMPEG_AVUTIL_LIBRARY}"
                    "${FFMPEG_SWSCALE_LIBRARY}"
                    "${FFMPEG_SWRESAMPLE_LIBRARY}")
                    get_filename_component(ffmpeg_library_dir "${ffmpeg_library}" DIRECTORY)
                    list(APPEND BINK_COMPAT_FFMPEG_SEARCH_DIRS "${ffmpeg_library_dir}")
                endforeach()
            endif()
        endif()

        if(WIN32)
            if(BINK_COMPAT_FFMPEG_HINT_ROOT AND EXISTS "${BINK_COMPAT_FFMPEG_HINT_ROOT}/bin")
                set(BINK_COMPAT_FFMPEG_RUNTIME_DIR "${BINK_COMPAT_FFMPEG_HINT_ROOT}/bin")
            elseif(DEFINED ENV{FFMPEG_ROOT} AND EXISTS "$ENV{FFMPEG_ROOT}/bin")
                set(BINK_COMPAT_FFMPEG_RUNTIME_DIR "$ENV{FFMPEG_ROOT}/bin")
            elseif(DEFINED ENV{FFMPEG_DIR} AND EXISTS "$ENV{FFMPEG_DIR}/bin")
                set(BINK_COMPAT_FFMPEG_RUNTIME_DIR "$ENV{FFMPEG_DIR}/bin")
            endif()

            if(BINK_COMPAT_FFMPEG_RUNTIME_DIR)
                foreach(ffmpeg_component IN ITEMS avcodec avformat avutil swscale swresample)
                    string(TOUPPER "${ffmpeg_component}" ffmpeg_component_upper)
                    file(GLOB ffmpeg_component_dlls
                        LIST_DIRECTORIES FALSE
                        "${BINK_COMPAT_FFMPEG_RUNTIME_DIR}/${ffmpeg_component}*.dll")
                    if(ffmpeg_component_dlls)
                        list(SORT ffmpeg_component_dlls)
                        list(GET ffmpeg_component_dlls 0 ffmpeg_component_dll)
                        get_filename_component(ffmpeg_component_dll_name "${ffmpeg_component_dll}" NAME)
                        target_compile_definitions(bink_compat PRIVATE
                            "BINK_COMPAT_DLL_${ffmpeg_component_upper}=\"${ffmpeg_component_dll_name}\"")
                    endif()
                endforeach()
            endif()
        endif()

        if(BINK_COMPAT_FFMPEG_HINT_ROOT AND EXISTS "${BINK_COMPAT_FFMPEG_HINT_ROOT}/lib")
            list(APPEND BINK_COMPAT_FFMPEG_SEARCH_DIRS "${BINK_COMPAT_FFMPEG_HINT_ROOT}/lib")
        endif()

        list(REMOVE_DUPLICATES BINK_COMPAT_FFMPEG_SEARCH_DIRS)
        message(STATUS "bink_compat: FFmpeg backend enabled")
    elseif(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "bink_compat: FFmpeg backend is required for 64-bit Windows builds but could not be prepared or found")
    else()
        message(FATAL_ERROR "bink_compat: FFmpeg backend is required on this platform but could not be prepared or found")
    endif()
endif()

set(BINK_COMPAT_LIBRARY "bink_compat" PARENT_SCOPE)
set(BINK_COMPAT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include" PARENT_SCOPE)
set(BINK_COMPAT_FFMPEG_RUNTIME_DIR "${BINK_COMPAT_FFMPEG_RUNTIME_DIR}" PARENT_SCOPE)
set(BINK_COMPAT_FFMPEG_SEARCH_DIRS "${BINK_COMPAT_FFMPEG_SEARCH_DIRS}" PARENT_SCOPE)
set(BINK_COMPAT_FFMPEG_SOURCE "${BINK_COMPAT_FFMPEG_SOURCE}" PARENT_SCOPE)
