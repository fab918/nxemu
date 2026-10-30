# FFmpeg's checked-in Android archives have no MediaCodec decoders. Build the
# matching 7.0.3 release with CPU-readable NDK MediaCodec output and software
# fallbacks instead of shipping an option which cannot select those decoders.
include(ExternalProject)

if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "The Android FFmpeg source build currently supports arm64-v8a")
endif()
get_filename_component(_ffmpeg_toolchain_bin "${CMAKE_C_COMPILER}" DIRECTORY)
set(_ffmpeg_prefix "${CMAKE_BINARY_DIR}/external/android-ffmpeg/install")
file(MAKE_DIRECTORY "${_ffmpeg_prefix}/include")
find_program(_ffmpeg_make NAMES gmake make REQUIRED NO_CMAKE_FIND_ROOT_PATH)
find_program(_ffmpeg_shell NAMES sh REQUIRED NO_CMAKE_FIND_ROOT_PATH)
# Gradle prepends the NDK bin directory to PATH. Its compiler is not the host CC.
unset(_ffmpeg_host_cc CACHE)
find_program(_ffmpeg_host_cc NAMES cc clang PATHS /usr/bin /bin NO_DEFAULT_PATH)
if(NOT _ffmpeg_host_cc)
    find_program(_ffmpeg_host_cc NAMES gcc cc clang REQUIRED NO_CMAKE_FIND_ROOT_PATH)
endif()
if(ANDROID_PLATFORM_LEVEL)
    set(_ffmpeg_api "${ANDROID_PLATFORM_LEVEL}")
else()
    string(REGEX REPLACE "^android-" "" _ffmpeg_api "${ANDROID_PLATFORM}")
endif()
if(NOT _ffmpeg_api MATCHES "^[0-9]+$" OR _ffmpeg_api LESS 21)
    message(FATAL_ERROR "Could not determine a supported Android API from ${ANDROID_PLATFORM}")
endif()
set(_ffmpeg_compiler_suffix "")
if(CMAKE_HOST_WIN32)
    set(_ffmpeg_compiler_suffix ".cmd")
endif()
set(_ffmpeg_jobs "${NXEMU_COMPILE_JOBS}")
if(NOT _ffmpeg_jobs)
    set(_ffmpeg_jobs 2)
endif()
set(_ffmpeg_names avfilter avcodec swscale avutil)
set(_ffmpeg_archives "")
foreach(_name IN LISTS _ffmpeg_names)
    list(APPEND _ffmpeg_archives "${_ffmpeg_prefix}/lib/lib${_name}.a")
endforeach()
ExternalProject_Add(nxemu_android_ffmpeg
    PREFIX "${CMAKE_BINARY_DIR}/external/android-ffmpeg"
    URL "https://ffmpeg.org/releases/ffmpeg-7.0.3.tar.xz"
    URL_HASH SHA256=bbb18f3cc5c1c34b21cf176c0168c4d9aa97697542a97f60d491a58a814dea65
    DOWNLOAD_NO_PROGRESS TRUE
    CONFIGURE_COMMAND "${_ffmpeg_shell}" <SOURCE_DIR>/configure
        "--prefix=${_ffmpeg_prefix}"
        --enable-cross-compile --target-os=android --arch=aarch64 --cpu=armv8-a
        "--cc=${_ffmpeg_toolchain_bin}/aarch64-linux-android${_ffmpeg_api}-clang${_ffmpeg_compiler_suffix}"
        "--cxx=${_ffmpeg_toolchain_bin}/aarch64-linux-android${_ffmpeg_api}-clang++${_ffmpeg_compiler_suffix}"
        "--host-cc=${_ffmpeg_host_cc}"
        "--ar=${_ffmpeg_toolchain_bin}/llvm-ar"
        "--ranlib=${_ffmpeg_toolchain_bin}/llvm-ranlib"
        "--nm=${_ffmpeg_toolchain_bin}/llvm-nm"
        "--strip=${_ffmpeg_toolchain_bin}/llvm-strip"
        --enable-static --disable-shared --enable-pic --disable-programs
        --disable-doc --disable-debug --disable-network --disable-autodetect
        --disable-everything --disable-avdevice --disable-avformat
        --disable-postproc --disable-swresample --enable-avcodec --enable-avutil
        --enable-avfilter --enable-swscale --enable-mediacodec --enable-jni
        --enable-decoder=h264,vp8,vp9,h264_mediacodec,vp9_mediacodec
        --enable-parser=h264,vp9 --enable-bsf=h264_mp4toannexb
        --enable-filter=yadif
        COMMAND "${CMAKE_COMMAND}" -DFFMPEG_BUILD_DIR=<BINARY_DIR>
            -P "${CMAKE_CURRENT_LIST_DIR}/nxemu_check_android_ffmpeg.cmake"
    BUILD_COMMAND "${_ffmpeg_make}" "-j${_ffmpeg_jobs}"
        COMMAND "${_ffmpeg_make}" install
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS ${_ffmpeg_archives}
)
set(FFmpeg_INCLUDE_DIR "${_ffmpeg_prefix}/include")
set(FFmpeg_LIBRARIES ${_ffmpeg_archives})
