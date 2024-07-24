include(FetchContent)
include(ExternalProject)

# Avoid timestamp warnings for URL downloads
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
    cmake_policy(SET CMP0135 NEW)
endif()

# https://gitlab.gnome.org/GNOME/libxml2
macro(fetch_libxml2)
    FetchContent_Declare(
        LibXml2
        GIT_REPOSITORY https://gitlab.gnome.org/GNOME/libxml2
        GIT_TAG        2e9f7860a9cb8be29eca90b7409ef0278d30ef10 # v2.11.4
        SYSTEM
        FIND_PACKAGE_ARGS
        )
        set(LIBXML2_WITH_LZMA OFF CACHE INTERNAL "")
        set(LIBXML2_WITH_ZLIB OFF CACHE INTERNAL "")
        set(LIBXML2_WITH_PYTHON OFF CACHE INTERNAL "")
        set(LIBXML2_WITH_PROGRAMS OFF CACHE INTERNAL "")
        set(LIBXML2_WITH_TESTS OFF CACHE INTERNAL "")
        FetchContent_GetProperties(LibXml2)
        if(NOT libxml2_POPULATED)
            find_package(LibXml2 QUIET)
            if (LibXml2_FOUND)
                set(libxml2_POPULATED TRUE)
            else()
                FetchContent_Populate(LibXml2)
                add_subdirectory(${libxml2_SOURCE_DIR} ${libxml2_BINARY_DIR} EXCLUDE_FROM_ALL SYSTEM)
            endif()
        endif()
endmacro()

macro(fetch_curl)
    FetchContent_Declare(
        CURL
        GIT_REPOSITORY https://github.com/curl/curl/
        GIT_TAG 50490c0679fcd0e50bb3a8fbf2d9244845652cf0 # V8.2.1
        SYSTEM
        FIND_PACKAGE_ARGS
    )
    set(CURL_ENABLE_SSL OFF CACHE INTERNAL "")
    set(BUILD_CURL_EXE OFF CACHE INTERNAL "")
    set(HTTP_ONLY ON CACHE INTERNAL "")
    FetchContent_GetProperties(CURL)
    if(NOT curl_POPULATED)
        find_package(CURL QUIET)
        if (CURL_FOUND)
            set(curl_POPULATED TRUE)
        else()
            FetchContent_Populate(CURL)
            add_subdirectory(${curl_SOURCE_DIR} ${curl_BINARY_DIR} EXCLUDE_FROM_ALL SYSTEM)
        endif()
    endif()
endmacro()

# https://github.com/bji/libs3
set(LIBS3_PATCH_COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/infra/cmake/libs3_wrapper/ <SOURCE_DIR>)

if (NOT EXISTS ${CMAKE_BINARY_DIR}/_deps/libs3-src/src/request.c.orig)
set(LIBS3_PATCH0_COMMAND patch -p1 < ${PROJECT_SOURCE_DIR}/infra/cmake/libs3_wrapper/libs3_low_speed_limit.patch)
else()
set(LIBS3_PATCH0_COMMAND uname || true)
endif(NOT EXISTS ${CMAKE_BINARY_DIR}/_deps/libs3-src/src/request.c.orig)

macro(fetch_libs3)
    FetchContent_Declare(
        libs3
        GIT_REPOSITORY https://github.com/bji/libs3
        GIT_TAG        287e4bee6fd430ffb52604049de80a27a77ff6b4 # master
        PATCH_COMMAND ${LIBS3_PATCH_COMMAND}
        COMMAND ${LIBS3_PATCH0_COMMAND}
        SYSTEM
        FIND_PACKAGE_ARGS
        )
        FetchContent_GetProperties(libs3)
        if(NOT libs3_POPULATED)
            FetchContent_Populate(libs3)
            add_subdirectory(${libs3_SOURCE_DIR} ${libs3_BINARY_DIR} EXCLUDE_FROM_ALL SYSTEM)
        endif()
        if(NOT TARGET libs3::s3)
            add_library(libs3::s3 ALIAS s3)
        endif()
endmacro()

