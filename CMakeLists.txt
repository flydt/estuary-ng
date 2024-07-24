cmake_minimum_required(VERSION 3.25)

project(estuary VERSION 0.0.2 LANGUAGES C)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/infra/cmake)

# This is for warnings coming from third-party libraries coming from FetchContent
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)

include(GNUInstallDirs)
set(CMAKE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(src)

install(TARGETS estuary_s3copytool 
        RUNTIME 
                DESTINATION bin
        )

set(CPACK_PACKAGE_VENDOR "Irish Centre for High End Computing (ICHEC)")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Estuary - lustre copytool for s3 backends")
set(CPACK_PACKAGE_DESCRIPTION "Estuary - lustre copytool for s3 backends")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
set(CPACK_RESOURCE_FILE_README ${CMAKE_CURRENT_SOURCE_DIR}/README.md)
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/flydt/estuary-ng")
set(CPACK_THREADS ${CMAKE_BUILD_PARALLEL_LEVEL})

set(CPACK_RPM_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_RPM_PACKAGE_SUMMARY "Estuary - lustre copytool for s3 backends")
set(CPACK_RPM_PACKAGE_AUTOREQ "no")
set(CPACK_RPM_PACKAGE_LICENSE "GNU GPL")
set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")

set(CPACK_RPM_PACKAGE_REQUIRES "curl, lz4, libcurl, openssl, libconfig, libxml2, libbsd")
set(CPACK_RPM_INSTALL_WITH_EXEC ON)

set(CPACK_GENERATOR RPM)
set(CPACK_SOURCE_IGNORE_FILES
    /.git*
    /.clang*
    /.vscode
    /dist
    /examples
    /infra/containers
    /infra/ansible
    /infra/deps
    build/
    .cache/
    /\\\\.DS_Store
)

SET(CMAKE_BUILD_TYPE "Debug")
SET(CMAKE_C_FLAGS_DEBUG "$ENV{CFLAGS} -O0 -Wall -g -ggdb")
SET(CMAKE_C_FLAGS_RELEASE "$ENV{CFLAGS} -O3 -Wall")

include(CPack)
