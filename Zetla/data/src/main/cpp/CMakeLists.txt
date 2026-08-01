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

add_library(zetla SHARED
    ${ZETLA_ROOT}/zetla/api/dll_api.cpp
    ${ZETLA_ROOT}/zetla/api/jni_bridge.cpp
    ${ZETLA_ROOT}/zetla/session/session_manager.cpp
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
    # RAG module (hyperbolic search) - only files Android app needs
    ${ZETLA_ROOT}/zetla/rag/core/lorentz.cpp
    ${ZETLA_ROOT}/zetla/rag/index/lorentz_index.cpp
    ${ZETLA_ROOT}/zetla/rag/chunker/chunker.cpp
    ${ZETLA_ROOT}/zetla/rag/rag_tool.cpp
    ${ZETLA_ROOT}/zetla/rag/bert_tokenizer.cpp
    ${ZETLA_ROOT}/zetla/rag/hyp_embedder.cpp
    ${ZETLA_ROOT}/zetla/rag/graph_enhance.cpp
    ${ZETLA_ROOT}/zetla/rag/bm25_index.cpp
    ${ZETLA_ROOT}/zetla/rag/zetla_rag.cpp
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
    ${ZETLA_ROOT}/zetla/pdf
    # RAG module includes
    ${ZETLA_ROOT}/zetla/rag
    ${ZETLA_ROOT}/zetla/rag/core
    ${ZETLA_ROOT}/zetla/rag/index
    ${ZETLA_ROOT}/zetla/rag/chunker
    ${ZETLA_ROOT}/zetla/rag/projector
    ${ZETLA_ROOT}/zetla/rag/api
    ${ZETLA_ROOT}/zetla/rag/gpu
    # Eigen3 (bundled for Android)
    ${CMAKE_CURRENT_SOURCE_DIR}/../libs/eigen3
    ${DEPS_DIR}/include
    ${DEPS_DIR}/include/openssl
)

add_library(curl_static SHARED IMPORTED)
set_target_properties(curl_static PROPERTIES
    IMPORTED_LOCATION ${DEPS_DIR}/lib/libcurl.a
)

add_library(ssl_static STATIC IMPORTED)
set_target_properties(ssl_static PROPERTIES
    IMPORTED_LOCATION ${DEPS_DIR}/lib/libssl.a
)

add_library(crypto_static STATIC IMPORTED)
set_target_properties(crypto_static PROPERTIES
    IMPORTED_LOCATION ${DEPS_DIR}/lib/libcrypto.a
)

target_link_libraries(zetla
    ${log-lib}
    curl_static
    ssl_static
    crypto_static
    z
    android
    vulkan
)

# OpenMP for parallel indexing/retrieval (NDK static)
target_compile_options(zetla PRIVATE -fopenmp)
target_link_options(zetla PRIVATE -fopenmp -static-openmp)

# 16KB page alignment for Android 15+ (API 35+)
target_link_options(zetla PRIVATE "-Wl,-z,max-page-size=16384")

message(STATUS "ZETLA_SRC_DIR: ${ZETLA_SRC_DIR}")
message(STATUS "ZETLA_ROOT:    ${ZETLA_ROOT}")
message(STATUS "DEPS_DIR:      ${DEPS_DIR}")
message(STATUS "curl:          ${DEPS_DIR}/lib/libcurl.a")
message(STATUS "ssl:           ${DEPS_DIR}/lib/libssl.a")
message(STATUS "crypto:        ${DEPS_DIR}/lib/libcrypto.a")
