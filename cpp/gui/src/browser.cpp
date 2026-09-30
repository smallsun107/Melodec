// ReNcm GUI - file-picker modal.

#include "app.hpp"

#include "tasks.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace rencm::app {

namespace fs = std::filesystem;

static const char* browser_title(BrowserMode m) {
    switch (m) {
        case BrowserMode::Files: return "Add .ncm files";
        case BrowserMode::Folder: return "Add folder";
        case BrowserMode::OutputDir: return "Choose output folder";
        default: return "Browse";
    }
}

void open_browser(App& app, BrowserMode m) {
    app.browser_mode = m;
    app.browser_selected.clear();
    if (app.browser_dir.empty()) {
        const char* home = std::getenv("HOME");
        app.browser_dir = home ? home : "/";
    }
    app.browser_popup_open = true;
}

static void close_browser(App& app) {
    app.browser_popup_open = false;
    app.browser_mode = BrowserMode::None;
}

void draw_browser(App& app) {
    if (!app.browser_popup_open) return;

    ImGui::OpenPopup("##browser");
    ImGui::SetNextWindowSize(ImVec2(660, 500), ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal("##browser", &app.browser_popup_open,
                                ImGuiWindowFlags_NoSavedSettings))
        return;
    if (!app.browser_popup_open) {  // closed via the title-bar X
        ImGui::EndPopup();
        app.browser_mode = BrowserMode::None;
        return;
    }

    const BrowserMode mode = app.browser_mode;
    ImGui::TextUnformatted(browser_title(mode));
    ImGui::Separator();

    // location bar
    if (ImGui::Button("Up")) {
        fs::path p(app.browser_dir);
        if (p.has_parent_path() && p.parent_path() != p) app.browser_dir = p.parent_path().string();
    }
    ImGui::SameLine();
    if (ImGui::Button("Home")) {
        const char* home = std::getenv("HOME");
        if (home) app.browser_dir = home;
    }
    ImGui::SameLine();
    char pathbuf[2048];
    std::snprintf(pathbuf, sizeof(pathbuf), "%s", app.browser_dir.c_str());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##path", pathbuf, sizeof(pathbuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
        std::error_code ec;
        if (fs::is_directory(pathbuf, ec)) app.browser_dir = pathbuf;
    }

    // listing: directories first, then .ncm files
    std::error_code ec;
    std::vector<fs::directory_entry> dirs, files;
    for (auto& e : fs::directory_iterator(app.browser_dir, ec)) {
        if (e.is_directory(ec)) dirs.push_back(e);
        else if (e.is_regular_file(ec) && is_ncm(e.path())) files.push_back(e);
    }
    auto by_name = [](const fs::directory_entry& a, const fs::directory_entry& b) {
        return a.path().filename().string() < b.path().filename().string();
    };
    std::sort(dirs.begin(), dirs.end(), by_name);
    std::sort(files.begin(), files.end(), by_name);

    ImGui::BeginChild("##list", ImVec2(0, -40), true);
    for (auto& d : dirs) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.769f, 0.655f, 0.906f, 1.0f)); // iris
        std::string label = "[dir]  " + d.path().filename().string();
        const bool clicked = ImGui::Selectable(label.c_str());
        ImGui::PopStyleColor();
        if (clicked) app.browser_dir = d.path().string();  // single click enters
    }
    if (mode == BrowserMode::Files) {
        for (auto& f : files) {
            const bool sel = (f.path().string() == app.browser_selected);
            std::string label = "[ncm]  " + f.path().filename().string();
            if (ImGui::Selectable(label.c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick)) {
                app.browser_selected = f.path().string();
                if (ImGui::IsMouseDoubleClicked(0)) {
                    add_input(app, app.browser_selected);
                    app.browser_selected.clear();
                }
            }
        }
    }
    ImGui::EndChild();

    // actions
    if (mode == BrowserMode::Files) {
        const bool has = !app.browser_selected.empty() && fs::is_regular_file(app.browser_selected);
        ImGui::BeginDisabled(!has);
        if (ImGui::Button("Add selected file")) {
            add_input(app, app.browser_selected);
            app.browser_selected.clear();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (has)
            ImGui::TextDisabled("%s", fs::path(app.browser_selected).filename().string().c_str());
        else
            ImGui::TextDisabled("(select a .ncm file)");
    } else if (mode == BrowserMode::Folder) {
        if (ImGui::Button("Add this folder")) {
            add_input(app, app.browser_dir);
            close_browser(app);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(decrypts every .ncm inside; subfolders when enabled)");
    } else if (mode == BrowserMode::OutputDir) {
        if (ImGui::Button("Use this folder")) {
            app.output_dir = app.browser_dir;
            close_browser(app);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(audio and cover are written here)");
    }

    ImGui::SameLine();
    if (ImGui::Button("Close")) close_browser(app);

    ImGui::EndPopup();
}

} // namespace rencm::app
