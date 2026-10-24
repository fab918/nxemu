// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <windows.h>
#include "3rd_party/renderdoc/renderdoc_app.h"
#endif

// Opt-in diagnostic session, configured once before Vulkan initialisation.
// No RenderDoc loading, hotkey polling or rendering changes in normal launches.
inline void ConfigureRenderDocCapture()
{
    const char * directory = std::getenv("NXEMU_RENDERDOC_CAPTURE_DIR");
    if (!directory || !*directory) return;
#ifdef _WIN32
    const auto module = GetModuleHandleW(L"renderdoc.dll");
    const auto get_api = module ? reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI")) : nullptr;
    RENDERDOC_API_1_0_0 * api{};
    if (!get_api || !get_api(eRENDERDOC_API_Version_1_0_0, reinterpret_cast<void **>(&api)) || !api)
        throw std::runtime_error("GPU capture was requested, but RenderDoc is not attached. Use the RenderDoc F8 launcher.");
    const auto output = std::filesystem::u8path(directory);
    std::filesystem::create_directories(output);
    const auto name = (output / "nxemu").u8string();
    const std::string capture_template(name.begin(), name.end());
    RENDERDOC_InputButton key = eRENDERDOC_Key_F8;
    api->SetCaptureKeys(&key, 1);
    api->SetFocusToggleKeys(nullptr, 0);
    api->SetCaptureFilePathTemplate(capture_template.c_str());
    api->MaskOverlayBits(~0U, eRENDERDOC_Overlay_Default);
    std::ofstream ready(output / "capture-ready.txt", std::ios::trunc);
    ready << "RenderDoc attached before graphics initialisation.\nF8 captures the next frame to " << capture_template << "_frame*.rdc\n";
    if (!ready) throw std::runtime_error("Cannot save GPU capture readiness status");
#else
    throw std::runtime_error("This RenderDoc F8 launcher currently supports Windows only.");
#endif
}
