include(FetchDependencies)

fetch_curl()
fetch_libxml2()
fetch_libs3()

add_library(estuary_copytool OBJECT ct_common.c)
add_library(estuary_copytool_log OBJECT tlog.c)
add_library(estuary_copytool_growbuffer OBJECT growbuffer.c)
add_library(estuary_copytool_callback OBJECT s3_callback.c)

target_link_libraries(estuary_copytool PUBLIC 
    pthread 
    config 
    CURL::libcurl 
    LibXml2::LibXml2 
    bsd
    lustreapi )

add_executable(estuary_s3copytool lhsmtool_s3.c)

add_dependencies(estuary_s3copytool s3)

target_include_directories(estuary_copytool_callback PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../build/_deps/libs3-src/inc>
)

target_link_libraries(estuary_s3copytool PRIVATE estuary_copytool estuary_copytool_log estuary_copytool_growbuffer estuary_copytool_callback libs3::s3)
