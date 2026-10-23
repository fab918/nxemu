// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "dialog.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <nxemu-module-spec/video.h>
#include <thread>
class UiWindow;
class ShaderProfileDialog final : public Dialog, public IShaderProfileProgress
{
public:
    ShaderProfileDialog(UiWindow & window, const std::filesystem::path & assets, std::function<void(const std::string &)> start);
    ~ShaderProfileDialog();
    void Prepare(IVideo & video, bool importing, std::string game, uint64_t title, std::filesystem::path cache, std::string label);
    bool Busy() const
    {
        return m_busy.load();
    }
    void Cancel()
    {
        m_cancel = true;
    }
    void CancelAndWait();

private:
    void OnOpen() override;
    void OnUpdate() override;
    void OnCommand(const std::string & command) override;
    void OnCloseRequested() override;
    bool ProfileProgress(const char * phase, uint64_t done, uint64_t total) override;
    void Start();
    UiWindow & m_window;
    IVideo * m_video{};
    const std::function<void(const std::string &)> m_startGame;
    std::string m_game, m_label, m_update, m_profile, m_recording, m_root, m_cacheFile, m_index;
    std::filesystem::path m_cache;
    uint64_t m_title{};
    bool m_importing{}, m_finished{}, m_success{};
    std::atomic<bool> m_busy{}, m_cancel{};
    std::thread m_thread;
    std::mutex m_mutex;
    std::string m_phase, m_error;
    uint64_t m_done{}, m_total{};
    ShaderProfileResult m_result{};
};
