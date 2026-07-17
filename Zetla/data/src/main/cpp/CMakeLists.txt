cmake_minimum_required(VERSION 3.22.1)
project("zetla")

find_library(log-lib log)

# ZETLA_SRC_DIR is passed from Gradle (Points to project root = where deps_build/ lives)
# Fallback: walk up from this CMakeLists.txt to project root
if(NOT DEFINED ZETLA_SRC_DIR OR ZETLA_SRC_DIR STREQUAL "")
    get_filename_component(ZETLA_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../" ABSOLUTE)
endif()
set(ZETLA_ROOT ${ZETLA_SRC_DIR}/src)
set(DEPS_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/../libs/${ANDROID_ABI})
set(OPENSSL_LIB_DIR "${ZETLA_SRC_DIR}/deps_build/openssl/${ANDROID_ABI}/lib")

add_library(zetla SHARED
    ${ZETLA_ROOT}/zetla/api/dll_api.cpp
    ${ZETLA_ROOT}/zetla/api/jni_bridge.cpp
    ${ZETLA_ROOT}/zetla/session/session_manager.cpp
    ${ZETLA_ROOT}/zetla/search/duckduckgo_provider.cpp
    ${ZETLA_ROOT}/zetla/search/exa_provider.cpp
    ${ZETLA_ROOT}/zetla/search/web_search_tool.cpp
    ${ZETLA_ROOT}/zetla/storage/storage_manager.cpp
    ${ZETLA_ROOT}/zetla/storage/local/file_storage_backend.cpp
    ${ZETLA_ROOT}/zetla/storage/local/serialization.cpp
    ${ZETLA_ROOT}/zetla/storage/local/encryption.cpp
    ${ZETLA_ROOT}/zetla/file_handlers/base/file_handler_factory.cpp
    ${ZETLA_ROOT}/zetla/file_handlers/text/text_file_handler.cpp
    ${ZETLA_ROOT}/zetla/file_handlers/image/image_file_handler.cpp
    ${ZETLA_ROOT}/zetla/file_handlers/pdf/pdf_file_handler.cpp
    ${ZETLA_ROOT}/zetla/file_handlers/office/office_file_handler.cpp
    ${ZETLA_ROOT}/zetla/thirdparty/zip/zip.c
)

target_include_directories(zetla PRIVATE
    ${ZETLA_ROOT}
    ${ZETLA_ROOT}/zetla/thirdparty
    ${ZETLA_ROOT}/zetla/thirdparty/zip
    ${ZETLA_ROOT}/zetla/providers/opencode
    ${ZETLA_ROOT}/zetla/providers/nvidia
    ${ZETLA_ROOT}/zetla/providers/deepseek
    ${ZETLA_ROOT}/zetla/search
    ${ZETLA_ROOT}/zetla/route
    ${ZETLA_ROOT}/zetla/protocols/openai_chat
    ${ZETLA_ROOT}/zetla/tools
    ${DEPS_DIR}/include
    ${DEPS_DIR}/include/openssl
)

add_library(curl_static SHARED IMPORTED)
set_target_properties(curl_static PROPERTIES
    IMPORTED_LOCATION ${DEPS_DIR}/lib/libcurl.a
)

add_library(ssl_static STATIC IMPORTED)
set_target_properties(ssl_static PROPERTIES
    IMPORTED_LOCATION ${ZETLA_SRC_DIR}/deps_build/openssl/${ANDROID_ABI}/lib/libssl.a
)

add_library(crypto_static STATIC IMPORTED)
set_target_properties(crypto_static PROPERTIES
    IMPORTED_LOCATION ${ZETLA_SRC_DIR}/deps_build/openssl/${ANDROID_ABI}/lib/libcrypto.a
)

target_link_libraries(zetla
    ${log-lib}
    curl_static
    ssl_static
    crypto_static
    z
    android
)

# 16KB page alignment for Android 15+ (API 35+)
target_link_options(zetla PRIVATE "-Wl,-z,max-page-size=16384")

message(STATUS "ZETLA_SRC_DIR: ${ZETLA_SRC_DIR}")
message(STATUS "ZETLA_ROOT:    ${ZETLA_ROOT}")
message(STATUS "DEPS_DIR:      ${DEPS_DIR}")
message(STATUS "curl_static:   ${DEPS_DIR}/lib/libcurl.a")
message(STATUS "ssl_static:    ${ZETLA_SRC_DIR}/deps_build/openssl/${ANDROID_ABI}/lib/libssl.a")
message(STATUS "crypto_static: ${ZETLA_SRC_DIR}/deps_build/openssl/${ANDROID_ABI}/lib/libcrypto.a")
