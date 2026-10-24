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
    "${NXEMU_ROOT}/src/yuzu_common/cityhash.cpp"
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
    "${NXEMU_ROOT}/src/yuzu_common/cityhash.cpp"
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

add_executable(recorder-cost recorder_cost.cpp
    "${NXEMU_ROOT}/src/yuzu_video_core/renderer_vulkan/fixed_pipeline_state.cpp"
    "${NXEMU_ROOT}/src/yuzu_video_core/surface.cpp"
    "${NXEMU_ROOT}/src/yuzu_common/cityhash.cpp")
target_link_libraries(recorder-cost PRIVATE profile-storage)
target_compile_definitions(recorder-cost PRIVATE FMT_HEADER_ONLY)
if(MSVC)
    target_compile_options(recorder-cost PRIVATE /utf-8 /EHsc)
endif()

add_executable(recorder-replay recorder_replay.cpp
    "${NXEMU_ROOT}/src/yuzu_video_core/renderer_vulkan/fixed_pipeline_state.cpp"
    "${NXEMU_ROOT}/src/yuzu_video_core/surface.cpp"
    "${NXEMU_ROOT}/src/yuzu_common/cityhash.cpp")
target_link_libraries(recorder-replay PRIVATE profile-storage Threads::Threads)
target_compile_definitions(recorder-replay PRIVATE FMT_HEADER_ONLY)
if(MSVC)
    target_compile_options(recorder-replay PRIVATE /utf-8 /EHsc)
endif()
add_test(NAME recorder-replay COMMAND recorder-replay "${CMAKE_CURRENT_BINARY_DIR}/recorder-replay-")

add_executable(recorder-hotpath recorder_hotpath.cpp
    "${NXEMU_ROOT}/src/yuzu_video_core/renderer_vulkan/fixed_pipeline_state.cpp"
    "${NXEMU_ROOT}/src/yuzu_video_core/surface.cpp"
    "${NXEMU_ROOT}/src/yuzu_common/cityhash.cpp")
target_link_libraries(recorder-hotpath PRIVATE profile-storage Threads::Threads)
target_compile_definitions(recorder-hotpath PRIVATE FMT_HEADER_ONLY)
if(MSVC)
    target_compile_options(recorder-hotpath PRIVATE /utf-8 /EHsc)
endif()

add_executable(recorder-input-cache recorder_input_cache.cpp
    "${NXEMU_ROOT}/src/yuzu_video_core/renderer_vulkan/fixed_pipeline_state.cpp"
    "${NXEMU_ROOT}/src/yuzu_video_core/surface.cpp"
    "${NXEMU_ROOT}/src/yuzu_common/cityhash.cpp")
target_link_libraries(recorder-input-cache PRIVATE profile-storage Threads::Threads)
target_compile_definitions(recorder-input-cache PRIVATE FMT_HEADER_ONLY)
if(MSVC)
    target_compile_options(recorder-input-cache PRIVATE /utf-8 /EHsc)
endif()
add_test(NAME recorder-input-cache COMMAND recorder-input-cache)

# Optional frozen-header baseline, kept outside the source tree. Both binaries
# compile the same benchmark and differ only in the recorder headers under test.
set(NXSHADER_RECORDER_REFERENCE_INCLUDE "" CACHE PATH "Frozen recorder headers for A/B microbenchmarks")
if(NXSHADER_RECORDER_REFERENCE_INCLUDE)
    add_executable(recorder-hotpath-reference recorder_hotpath.cpp
        "${NXEMU_ROOT}/src/yuzu_video_core/renderer_vulkan/fixed_pipeline_state.cpp"
        "${NXEMU_ROOT}/src/yuzu_video_core/surface.cpp"
        "${NXEMU_ROOT}/src/yuzu_common/cityhash.cpp")
    target_include_directories(recorder-hotpath-reference BEFORE PRIVATE "${NXSHADER_RECORDER_REFERENCE_INCLUDE}")
    target_link_libraries(recorder-hotpath-reference PRIVATE profile-storage Threads::Threads)
    target_compile_definitions(recorder-hotpath-reference PRIVATE FMT_HEADER_ONLY)
    if(MSVC)
        target_compile_options(recorder-hotpath-reference PRIVATE /utf-8 /EHsc)
    endif()
endif()

if(WIN32)
    add_executable(native-present-publisher native_present_publisher.cpp)
    target_compile_features(native-present-publisher PRIVATE cxx_std_20)
    target_compile_definitions(native-present-publisher PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_executable(presentmon-capture presentmon_capture.cpp)
    set_target_properties(presentmon-capture PROPERTIES OUTPUT_NAME nxshader-presentmon-capture)
    target_compile_features(presentmon-capture PRIVATE cxx_std_20)
    target_compile_definitions(presentmon-capture PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_executable(recorder-session-monitor recorder_session_monitor.cpp)
    target_compile_features(recorder-session-monitor PRIVATE cxx_std_20)
    target_compile_definitions(recorder-session-monitor PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(recorder-session-monitor PRIVATE user32 psapi profile-storage)
endif()

add_executable(recorder-cache-trace recorder_cache_trace.cpp
    "${NXEMU_ROOT}/src/yuzu_common/cityhash.cpp")
target_link_libraries(recorder-cache-trace PRIVATE profile-storage)

add_executable(synthetic-robustness synthetic_robustness.cpp
    "${NXEMU_ROOT}/src/yuzu_video_core/renderer_vulkan/fixed_pipeline_state.cpp"
    "${NXEMU_ROOT}/src/yuzu_video_core/surface.cpp")
target_compile_definitions(synthetic-robustness PRIVATE FMT_HEADER_ONLY)
target_link_libraries(synthetic-robustness PRIVATE profile-storage Threads::Threads)
add_test(NAME synthetic-robustness COMMAND synthetic-robustness)
set_tests_properties(synthetic-robustness PROPERTIES TIMEOUT 120)

# Optional: benchmark the actual compiler archives from a completed app build.
set(NXSHADER_APP_BUILD "" CACHE PATH "Completed app build for the CPU shader compiler benchmark")
if(NXSHADER_APP_BUILD)
    add_executable(shader-compile-benchmark shader_compile_benchmark.cpp)
    target_link_libraries(shader-compile-benchmark PRIVATE profile-storage
        "${NXSHADER_APP_BUILD}/src/yuzu_shader_recompiler/${CMAKE_STATIC_LIBRARY_PREFIX}shader_recompiler${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "${NXSHADER_APP_BUILD}/external/sirit/src/${CMAKE_STATIC_LIBRARY_PREFIX}sirit${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "${NXSHADER_APP_BUILD}/external/fmt/${CMAKE_STATIC_LIBRARY_PREFIX}fmt${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "${NXSHADER_APP_BUILD}/src/yuzu_common/${CMAKE_STATIC_LIBRARY_PREFIX}yuzu_common${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "${NXSHADER_APP_BUILD}/src/common/${CMAKE_STATIC_LIBRARY_PREFIX}common${CMAKE_STATIC_LIBRARY_SUFFIX}")
endif()

add_executable(register-tracking-benchmark register_tracking_benchmark.cpp
    "${NXEMU_ROOT}/src/yuzu_video_core/renderer_vulkan/fixed_pipeline_state.cpp"
    "${NXEMU_ROOT}/src/yuzu_video_core/surface.cpp")
target_link_libraries(register-tracking-benchmark PRIVATE profile-storage Threads::Threads)
target_compile_definitions(register-tracking-benchmark PRIVATE FMT_HEADER_ONLY)
add_executable(cross-version-reuse cross_version_reuse.cpp)
target_link_libraries(cross-version-reuse PRIVATE profile-storage)
add_test(NAME cross-version-reuse COMMAND cross-version-reuse)
add_executable(recorded-sources recorded_sources.cpp
    "${NXEMU_ROOT}/src/yuzu_video_core/renderer_vulkan/fixed_pipeline_state.cpp"
    "${NXEMU_ROOT}/src/yuzu_video_core/surface.cpp")
target_link_libraries(recorded-sources PRIVATE profile-storage)
target_compile_definitions(recorded-sources PRIVATE FMT_HEADER_ONLY)
add_test(NAME recorded-sources COMMAND recorded-sources "${CMAKE_CURRENT_BINARY_DIR}/recorded-sources-fixture")
if(MSVC)
    target_compile_options(cross-version-reuse PRIVATE /utf-8 /EHsc)
    target_compile_options(recorded-sources PRIVATE /utf-8 /EHsc)
    target_compile_options(register-tracking-benchmark PRIVATE /utf-8 /EHsc)
    target_compile_options(synthetic-robustness PRIVATE /utf-8 /EHsc)
endif()
