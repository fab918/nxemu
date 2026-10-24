cmake_minimum_required(VERSION 3.22)
project(nxshader_guest_state_tests LANGUAGES C CXX)
add_library(yaz0-scan SHARED ../yaz0.cpp)
enable_testing()
add_executable(profile-metrics profile_metrics.cpp)
target_compile_features(profile-metrics PRIVATE cxx_std_20)
target_include_directories(profile-metrics PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../src")
find_package(Threads REQUIRED)
target_link_libraries(profile-metrics PRIVATE Threads::Threads)
add_test(NAME profile-metrics COMMAND profile-metrics)
get_filename_component(NXEMU_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory("${NXEMU_ROOT}/external/zstd/build/cmake" "${CMAKE_CURRENT_BINARY_DIR}/zstd" EXCLUDE_FROM_ALL)
target_sources(yaz0-scan PRIVATE ../profile_codec_api.cpp ../profile_search_api.cpp
    "${NXEMU_ROOT}/src/yuzu_video_core/shader_profile_codec.cpp")
target_include_directories(yaz0-scan PRIVATE "${NXEMU_ROOT}/src")
target_compile_features(yaz0-scan PRIVATE cxx_std_20)
target_link_libraries(yaz0-scan PRIVATE libzstd_static)
add_executable(profile-search profile_search.cpp)
target_compile_features(profile-search PRIVATE cxx_std_20)
target_include_directories(profile-search PRIVATE "${NXEMU_ROOT}/src")
add_test(NAME profile-search COMMAND profile-search)
add_executable(guest-pipeline-state guest_pipeline_state.cpp
    "${NXEMU_ROOT}/src/yuzu_video_core/renderer_vulkan/fixed_pipeline_state.cpp"
    "${NXEMU_ROOT}/src/yuzu_video_core/surface.cpp"
    "${NXEMU_ROOT}/src/yuzu_common/cityhash.cpp")
target_compile_features(guest-pipeline-state PRIVATE cxx_std_20)
target_include_directories(guest-pipeline-state PRIVATE "${NXEMU_ROOT}/src"
    "${NXEMU_ROOT}/src/nxemu-video"
    "${NXEMU_ROOT}/external/boost" "${NXEMU_ROOT}/external/fmt/include")
target_compile_definitions(guest-pipeline-state PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN FMT_HEADER_ONLY)
if(MSVC)
    target_compile_options(guest-pipeline-state PRIVATE /utf-8 /EHsc)
endif()
add_test(NAME guest-pipeline-state COMMAND guest-pipeline-state)

add_library(profile-storage STATIC
    "${NXEMU_ROOT}/src/yuzu_video_core/pipeline_cache_store.cpp"
    "${NXEMU_ROOT}/src/yuzu_video_core/shader_profile.cpp"
    "${NXEMU_ROOT}/src/yuzu_video_core/shader_profile_codec.cpp"
    "${NXEMU_ROOT}/src/yuzu_video_core/shader_profile_export.cpp"
    "${NXEMU_ROOT}/src/yuzu_video_core/shader_profile_discovery.cpp"
    "${NXEMU_ROOT}/src/yuzu_video_core/shader_profile_compact.cpp"
    "${NXEMU_ROOT}/src/common/sha256.cpp"
    "${NXEMU_ROOT}/src/common/dynamic_library.cpp"
    "${NXEMU_ROOT}/src/common/json.cpp"
    "${NXEMU_ROOT}/src/common/std_string.cpp"
    "${NXEMU_ROOT}/src/common/platform.cpp")
target_include_directories(profile-storage PUBLIC "${NXEMU_ROOT}/src"
    "${NXEMU_ROOT}/src/nxemu-video" "${NXEMU_ROOT}/external/boost"
    "${NXEMU_ROOT}/external/fmt/include")
target_compile_definitions(profile-storage PRIVATE FMT_HEADER_ONLY)
if(MSVC)
    target_compile_options(profile-storage PRIVATE /utf-8 /EHsc)
endif()
target_compile_features(profile-storage PUBLIC cxx_std_20)
target_compile_definitions(profile-storage PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(profile-storage PRIVATE libzstd_static ${CMAKE_DL_LIBS})
add_executable(nxshader-export profile_export.cpp
    "${NXEMU_ROOT}/src/yuzu_video_core/renderer_vulkan/fixed_pipeline_state.cpp"
    "${NXEMU_ROOT}/src/yuzu_video_core/surface.cpp"
    "${NXEMU_ROOT}/src/yuzu_common/cityhash.cpp")
target_link_libraries(nxshader-export PRIVATE profile-storage)
target_include_directories(nxshader-export PRIVATE "${NXEMU_ROOT}/src/nxemu-video"
    "${NXEMU_ROOT}/external/boost" "${NXEMU_ROOT}/external/fmt/include")
target_compile_definitions(nxshader-export PRIVATE FMT_HEADER_ONLY)
if(MSVC)
    target_compile_options(nxshader-export PRIVATE /utf-8 /EHsc)
endif()
add_executable(profile-hash profile_hash.cpp)
target_link_libraries(profile-hash PRIVATE profile-storage)
add_test(NAME profile-hash COMMAND profile-hash)
add_executable(nxshader-codec profile_codec.cpp)
target_link_libraries(nxshader-codec PRIVATE profile-storage)
add_executable(profile-codec-regression profile_codec_regression.cpp)
target_link_libraries(profile-codec-regression PRIVATE profile-storage)
add_test(NAME profile-codec COMMAND profile-codec-regression)
add_executable(cache-store cache_store.cpp)
target_link_libraries(cache-store PRIVATE profile-storage)
add_test(NAME cache-store COMMAND cache-store "${CMAKE_CURRENT_BINARY_DIR}/regression.nxc")
add_executable(recipe-journal recipe_journal.cpp)
target_link_libraries(recipe-journal PRIVATE profile-storage)
add_test(NAME recipe-journal COMMAND recipe-journal "${CMAKE_CURRENT_BINARY_DIR}/regression.nxr")
add_executable(startup-variants startup_variants.cpp)
target_link_libraries(startup-variants PRIVATE profile-storage)
if(MSVC)
    target_compile_options(startup-variants PRIVATE /utf-8 /EHsc)
endif()
add_test(NAME startup-variants COMMAND startup-variants)
add_executable(cache-store-benchmark cache_store_benchmark.cpp)
target_link_libraries(cache-store-benchmark PRIVATE profile-storage)
add_executable(nxshader-reader profile_reader.cpp
    "${NXEMU_ROOT}/src/yuzu_video_core/renderer_vulkan/fixed_pipeline_state.cpp"
    "${NXEMU_ROOT}/src/yuzu_video_core/surface.cpp"
    "${NXEMU_ROOT}/src/yuzu_common/cityhash.cpp")
target_link_libraries(nxshader-reader PRIVATE profile-storage)
target_include_directories(nxshader-reader PRIVATE "${NXEMU_ROOT}/src/nxemu-video"
    "${NXEMU_ROOT}/external/boost" "${NXEMU_ROOT}/external/fmt/include")
target_compile_definitions(nxshader-reader PRIVATE FMT_HEADER_ONLY)
if(MSVC)
    target_compile_options(nxshader-reader PRIVATE /utf-8 /EHsc)
endif()

add_executable(recipe-projection recipe_projection.cpp
    "${NXEMU_ROOT}/src/yuzu_video_core/renderer_vulkan/fixed_pipeline_state.cpp"
    "${NXEMU_ROOT}/src/yuzu_video_core/surface.cpp"
    "${NXEMU_ROOT}/src/yuzu_common/cityhash.cpp")
target_compile_features(recipe-projection PRIVATE cxx_std_20)
target_include_directories(recipe-projection PRIVATE "${NXEMU_ROOT}/src"
    "${NXEMU_ROOT}/src/nxemu-video" "${NXEMU_ROOT}/external/boost"
    "${NXEMU_ROOT}/external/fmt/include")
target_compile_definitions(recipe-projection PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN FMT_HEADER_ONLY)
if(MSVC)
    target_compile_options(recipe-projection PRIVATE /utf-8 /EHsc)
endif()

add_executable(quarantine-runtime quarantine_runtime.cpp)
target_link_libraries(quarantine-runtime PRIVATE profile-storage Threads::Threads)
if(MSVC)
    target_compile_options(quarantine-runtime PRIVATE /utf-8 /EHsc)
endif()
add_test(NAME quarantine-runtime COMMAND quarantine-runtime "${CMAKE_CURRENT_BINARY_DIR}/runtime-report.json")

add_executable(cache-inventory cache_inventory.cpp)
target_link_libraries(cache-inventory PRIVATE profile-storage)
target_sources(cache-inventory PRIVATE "${NXEMU_ROOT}/src/yuzu_common/cityhash.cpp")
if(MSVC)
    target_compile_options(cache-inventory PRIVATE /utf-8 /EHsc)
endif()

add_executable(resource-gap-audit resource_gap_audit.cpp
    "${NXEMU_ROOT}/src/yuzu_video_core/renderer_vulkan/fixed_pipeline_state.cpp"
    "${NXEMU_ROOT}/src/yuzu_video_core/surface.cpp"
    "${NXEMU_ROOT}/src/yuzu_common/cityhash.cpp")
target_link_libraries(resource-gap-audit PRIVATE profile-storage)
target_compile_definitions(resource-gap-audit PRIVATE FMT_HEADER_ONLY)
if(MSVC)
    target_compile_options(resource-gap-audit PRIVATE /utf-8 /EHsc)
endif()
