cmake_minimum_required(VERSION 3.22)
project(nxshader_guest_state_tests LANGUAGES CXX)
add_library(yaz0-scan SHARED ../yaz0.cpp)
enable_testing()
get_filename_component(NXEMU_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
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
    "${NXEMU_ROOT}/src/common/sha256.cpp"
    "${NXEMU_ROOT}/src/common/json.cpp"
    "${NXEMU_ROOT}/src/common/std_string.cpp"
    "${NXEMU_ROOT}/src/common/platform.cpp")
target_include_directories(profile-storage PUBLIC "${NXEMU_ROOT}/src")
target_compile_features(profile-storage PUBLIC cxx_std_20)
target_compile_definitions(profile-storage PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
add_executable(cache-store cache_store.cpp)
target_link_libraries(cache-store PRIVATE profile-storage)
add_test(NAME cache-store COMMAND cache-store "${CMAKE_CURRENT_BINARY_DIR}/regression.nxc")
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
