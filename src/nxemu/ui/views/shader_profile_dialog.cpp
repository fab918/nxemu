// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader_profile_dialog.h"
#include "yuzu_video_core/shader_profile_location.h"
#include <RmlUi/Core.h>
#include <chrono>
#include <common/json.h>
#include <common/path.h>
#include <fstream>
#include <nxemu/ui/host/ui_window.h>
#include <nxemu/ui/i18n/translation.h>
#include <nxemu/ui/platform/file_dialogs.h>
#include <stdexcept>
#include <yuzu_common/fs/fs.h>
#ifdef _WIN32
#include <windows.h>
#endif
namespace
{
std::string Utf8(const std::filesystem::path & path)
{
    return Common::FS::PathToUTF8String(path);
}
void AtomicWrite(const std::filesystem::path & path, std::string_view bytes)
{
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), bytes.size());
    out.flush();
    if (!out) throw std::runtime_error("Cannot write shader profile metadata");
    out.close();
#ifdef _WIN32
    if (!MoveFileExW(std::filesystem::path(temporary).c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot publish shader profile metadata");
#else
    std::filesystem::rename(temporary, path);
#endif
}
std::string ReadSmall(const std::filesystem::path & path, size_t limit)
{
    const auto size = std::filesystem::file_size(path);
    if (size > limit) throw std::runtime_error("Shader profile size limit");
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read shader profile");
    std::string bytes(static_cast<size_t>(size), '\0');
    in.read(bytes.data(), static_cast<std::streamsize>(size));
    if (!in || in.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Shader profile changed during reading");
    return bytes;
}
} // namespace
ShaderProfileDialog::ShaderProfileDialog(UiWindow & window, const std::filesystem::path & assets, std::function<void(const std::string &)> start) :
    Dialog(window.Context(), assets / "shader_profile_dialog.rml"), m_window(window), m_startGame(std::move(start))
{
}
ShaderProfileDialog::~ShaderProfileDialog()
{
    CancelAndWait();
}
void ShaderProfileDialog::CancelAndWait()
{
    m_cancel = true;
    if (m_thread.joinable()) m_thread.join();
}
void ShaderProfileDialog::Prepare(IVideo & video, bool importing, std::string game, uint64_t title, std::filesystem::path cache, std::string label)
{
    if (Busy()) return;
    CancelAndWait();
    m_video = &video;
    m_importing = importing;
    m_game = std::move(game);
    m_title = title;
    m_cache = std::move(cache);
    m_label = std::move(label);
    const auto path = std::filesystem::u8path(m_game);
    m_root = Utf8(path.parent_path());
    m_recording = Utf8(m_cache / "guest-recipes.nxr");
    m_cacheFile = Utf8(std::filesystem::exists(m_cache / "vulkan.nxc") ? m_cache / "vulkan.nxc" : m_cache / "vulkan.bin");
    m_index = Utf8(m_cache / "discovery-index.json");
    m_update.clear();
    if (std::filesystem::exists(m_cache / "nxshader-update.txt")) try
        {
            m_update = ReadSmall(m_cache / "nxshader-update.txt", 4096);
        }
        catch (...)
        {
        }
    m_cancel = false;
    m_finished = false;
    m_success = false;
    m_result = {};
    m_error.clear();
    Show();
}
void ShaderProfileDialog::OnOpen()
{
    SetText("profile-title", Tr(m_importing ? "Import shader profile" : "Export shader profile"));
    SetText("profile-start", Tr(m_importing ? "Choose profile" : "Export .nxshader"));
    SetText("profile-game", m_label);
    SetText("profile-detail", Tr(m_importing ? "Select a profile. NXEmu verifies it against your local game before prewarming at game startup." : "The profile contains recipes and resource references. Shader instructions stay in your local game."));
    SetText("profile-update", m_update.empty() ? Tr("No update selected") : m_update);
    Document()->GetElementById("profile-update-row")->SetProperty("display", m_importing ? "none" : "block");
}
bool ShaderProfileDialog::ProfileProgress(const char * phase, uint64_t done, uint64_t total)
{
    std::scoped_lock lock(m_mutex);
    m_phase = phase;
    m_done = done;
    m_total = total;
    return !m_cancel.load();
}
void ShaderProfileDialog::Start()
{
    if (Busy()) return;
    // Completion does not make std::thread non-joinable; OnUpdate may not have
    // joined it yet when another operation is requested.
    if (m_thread.joinable()) m_thread.join();
    if (!m_importing && !std::filesystem::exists(std::filesystem::u8path(m_recording)))
    {
        SetText("profile-detail", Tr("Play normally with this version, stop the game, then export. Recipes are recorded automatically with the local shader cache."));
        return;
    }
    Path selected;
    const auto filter = FileFilter({{Tr("Shader profile"), "*.nxshader"}});
    const auto suggested=VideoCommon::ShaderProfile::ProfileFilename(m_title);
    const bool chosen = m_importing ? FileSelect(m_window.NativeHandle(), m_root.c_str(), filter.c_str(), true, selected) : FileSave(m_window.NativeHandle(), m_root.c_str(), filter.c_str(), suggested.c_str(), selected);
    if (!chosen) return;
    m_profile = static_cast<const char *>(selected);
    m_cancel = false;
    m_finished = false;
    m_result = {};
    m_busy = true;
    Document()->GetElementById("profile-start")->SetAttribute("disabled", "");
    Document()->GetElementById("profile-update-row")->SetAttribute("disabled", "");
    try
    {
        m_thread = std::thread([this] {
            bool success = false;
            std::string failure;
            ShaderProfileResult result{};
            try
            {
                std::filesystem::create_directories(m_cache);
                const auto game = std::filesystem::u8path(m_game);
                std::string profile = m_profile;
                if (m_importing)
                {
                    const auto snapshot = ReadSmall(std::filesystem::u8path(m_profile), 33 * 1024 * 1024);
                    const auto staged = m_cache / "community-pending.nxshader";
                    AtomicWrite(staged, snapshot);
                    profile = Utf8(staged);
                }
                const auto base = Utf8(game.filename());
                ShaderProfileTask task{};
                task.importing = m_importing;
                task.compact = true;
                task.title_id = m_title;
                task.cache = m_cacheFile.c_str();
                task.recording = m_recording.c_str();
                task.game_root = m_root.c_str();
                task.base = base.c_str();
                task.update = m_update.c_str();
                task.profile = profile.c_str();
                task.index = m_index.c_str();
                task.label = m_label.c_str();
                char error[1024]{};
                success = m_video->ProcessShaderProfile(task, *this, result, error, sizeof(error));
                if (!success) failure = error;
                if (success && m_importing && m_cancel)
                {
                    success = false;
                    failure = "Shader profile operation cancelled";
                }
                if (success && m_importing)
                {
                    const auto name = "community-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".nxshader";
                    std::filesystem::rename(std::filesystem::u8path(profile), m_cache / name);
                    JsonValue selection;
                    selection["file"] = name;
                    selection["root"] = m_root;
                    AtomicWrite(m_cache / "community.json", JsonStyledWriter().write(selection));
                }
                if (success && !m_importing) AtomicWrite(m_cache / "nxshader-update.txt", m_update);
            }
            catch (const std::exception & error)
            {
                success = false;
                failure = error.what();
            }
            {
                std::scoped_lock lock(m_mutex);
                m_success = success;
                m_error = std::move(failure);
                m_result = result;
                m_finished = true;
            }
            m_busy = false;
        });
    }
    catch (const std::exception & error)
    {
        m_busy = false;
        m_success = false;
        m_finished = true;
        m_error = error.what();
    }
}
void ShaderProfileDialog::OnUpdate()
{
    if (!Busy() && m_thread.joinable()) m_thread.join();
    std::scoped_lock lock(m_mutex);
    if (m_finished)
    {
        if(m_success && m_importing)
            SetText("profile-detail",Tr("Profile selected. Local shaders will be verified when the game starts."));
        else if (m_success)
            SetText("profile-detail", Tr("%1 graphics recipes, %2 compute recipes, %3 unresolved observations, %4 missing programs. %5 bytes.", {std::to_string(m_result.graphics), std::to_string(m_result.compute), std::to_string(m_result.unresolved), std::to_string(m_result.missing), std::to_string(m_result.bytes)}));
        else
            SetText("profile-detail", m_error);
        SetText("profile-status", Tr(m_success ? "Complete" : "Operation stopped"));
        SetText("profile-start", Tr(m_success && m_importing ? "Start game" : "Close"));
        Document()->GetElementById("profile-start")->RemoveAttribute("disabled");
    }
    else if (Busy())
    {
        const char * status = m_phase == "verify-index" ? "Verifying saved references" : m_phase == "raw"  ? "Searching local game data"
                                                                                     : m_phase == "verify" ? "Verifying local shaders"
                                                                                                           : "Searching game resources";
        SetText("profile-status", Tr(status) + (m_total ? " " + std::to_string(m_done) + " / " + std::to_string(m_total) : ""));
    }
}
void ShaderProfileDialog::OnCloseRequested()
{
    if (Busy())
    {
        Cancel();
        SetText("profile-status", Tr("Cancelling"));
    }
    else
        Close();
}
void ShaderProfileDialog::OnCommand(const std::string & command)
{
    if (command == "close")
    {
        OnCloseRequested();
        return;
    }
    if (Busy()) return;
    if (command == "start")
    {
        if (m_finished)
        {
            const bool play = m_success && m_importing;
            Close();
            if (play) m_startGame(m_game);
        }
        else
            Start();
        return;
    }
    if (Busy()) return;
    if (command == "no-update")
    {
        m_update.clear();
        SetText("profile-update", Tr("No update selected"));
        return;
    }
    if (command == "update")
    {
        Path selected;
        const auto filter = FileFilter({{Tr("Game update"), "*.dnsp"}});
        if (FileSelect(m_window.NativeHandle(), m_root.c_str(), filter.c_str(), true, selected))
        {
            const auto path = std::filesystem::u8path(static_cast<const char *>(selected));
            if (std::filesystem::canonical(path.parent_path()) != std::filesystem::canonical(std::filesystem::u8path(m_root)))
            {
                SetText("profile-detail", Tr("This prototype needs the game and its update in the same folder."));
                return;
            }
            m_update = Utf8(path.filename());
            SetText("profile-update", m_update);
        }
    }
}
