// ReNcm GUI - window, layout and main loop.
//
// Shared state lives in app.hpp; the file-picker modal (browser.cpp), the job
// runner (jobs.cpp), the transport bar (playerbar.cpp) and the theme
// (theme.cpp) are separate translation units.

#include "ui.hpp"

#include "app.hpp"
#include "embedded_font.hpp"
#include "theme.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <chrono>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace rencm::app {

namespace fs = std::filesystem;

// Height reserved at the bottom of the window for the player panel.
constexpr float kPlayerBarH = 148.0f;

// ImGui::ProgressBar() parks its overlay label just after the filled part (only
// an indeterminate bar centres it), so draw the bar bare and centre the text
// ourselves - with a shadow, since it can sit across the fill boundary.
static void centered_progress(float fraction, const char* label) {
    ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0), "");

    const ImVec2 p0 = ImGui::GetItemRectMin();
    const ImVec2 p1 = ImGui::GetItemRectMax();
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const ImVec2 tp((p0.x + p1.x - ts.x) * 0.5f, (p0.y + p1.y - ts.y) * 0.5f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(ImVec2(tp.x + 1.0f, tp.y + 1.0f), IM_COL32(0, 0, 0, 130), label);
    dl->AddText(tp, ImGui::GetColorU32(ImGuiCol_Text), label);
}

static void draw_ui(App& app) {
    // consume drag & drop
    {
        std::lock_guard<std::mutex> lk(app.mtx);
        for (auto& p : app.pending_drop) add_input(app, p);
        app.pending_drop.clear();
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("ReNcm", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::TextUnformatted("ReNcm - NCM decoder");
    ImGui::SameLine();
    ImGui::TextDisabled("(drop .ncm files or folders into this window)");
    ImGui::Separator();

    const bool busy = app.running.load();

    // Uniform control sizing: every button is the same width, and every text
    // field ends exactly where the button column starts, so rows line up.
    const float kBtn = 128.0f;
    const float kField = -(kBtn + ImGui::GetStyle().ItemSpacing.x);

    // ---- sources ----
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Add files...", ImVec2(kBtn, 0))) open_browser(app, BrowserMode::Files);
    ImGui::SameLine();
    if (ImGui::Button("Add folder...", ImVec2(kBtn, 0))) open_browser(app, BrowserMode::Folder);
    ImGui::SameLine();
    if (ImGui::Button("Clear list", ImVec2(kBtn, 0))) {
        app.inputs.clear();
        std::lock_guard<std::mutex> lk(app.mtx);
        app.jobs.clear();
        app.current.clear();
        app.status.clear();
    }

    static char addbuf[2048] = "";
    ImGui::SetNextItemWidth(kField);
    if (ImGui::InputTextWithHint("##addpath", "or paste a file / folder path here", addbuf,
                                 sizeof(addbuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (addbuf[0]) add_input(app, addbuf);
        addbuf[0] = '\0';
    }
    ImGui::SameLine();
    if (ImGui::Button("Add path", ImVec2(kBtn, 0))) {
        if (addbuf[0]) add_input(app, addbuf);
        addbuf[0] = '\0';
    }

    ImGui::Text("Sources (%zu)", app.inputs.size());
    ImGui::BeginChild("##inputs", ImVec2(0, ImGui::GetFrameHeightWithSpacing() * 3.0f), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (app.inputs.empty()) {
        ImGui::TextDisabled("No files or folders added yet");
    }
    for (size_t i = 0; i < app.inputs.size();) {
        ImGui::PushID((int)i);
        if (ImGui::SmallButton("x")) {
            app.inputs.erase(app.inputs.begin() + i);
            ImGui::PopID();
            continue;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(app.inputs[i].c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", app.inputs[i].c_str());
        ImGui::PopID();
        ++i;
    }
    ImGui::EndChild();
    ImGui::EndDisabled();

    ImGui::Spacing();

    // ---- options ----
    ImGui::BeginDisabled(busy);
    char outbuf[1024];
    std::snprintf(outbuf, sizeof(outbuf), "%s", app.output_dir.c_str());
    ImGui::SetNextItemWidth(kField);
    if (ImGui::InputTextWithHint("##out", "Output folder (empty = next to source)", outbuf,
                                 sizeof(outbuf)))
        app.output_dir = outbuf;
    ImGui::SameLine();
    if (ImGui::Button("Choose...", ImVec2(kBtn, 0))) open_browser(app, BrowserMode::OutputDir);

    ImGui::Checkbox("Recurse subfolders", &app.recurse);
    ImGui::SameLine();
    ImGui::Checkbox("Export cover", &app.write_cover);
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ---- run ----
    ImGui::BeginDisabled(busy || app.inputs.empty());
    if (ImGui::Button("Decrypt", ImVec2(kBtn, 0))) start_jobs(app);
    ImGui::EndDisabled();
    if (!busy && !app.status.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", app.status.c_str());
    }

    if (busy) {
        // Overall: files done / total. Discrete and calm.
        const int done = app.done.load();
        char ov[64];
        std::snprintf(ov, sizeof(ov), "%d / %d files", done, app.total);
        centered_progress(app.total > 0 ? (float)done / (float)app.total : 0.0f, ov);

        // Per-file byte progress. Decryption is usually a few tens of ms, so a
        // raw bar would sweep in a couple of frames and just flicker; only show
        // it once a file has been running long enough to be worth watching, and
        // smooth the value so it never jumps.
        std::string cur_name;
        std::chrono::steady_clock::time_point cur_since{};
        {
            std::lock_guard<std::mutex> lk(app.mtx);
            cur_name = app.current;
            cur_since = app.current_since;
        }
        const uint64_t cd = app.cur_done.load();
        const uint64_t ct = app.cur_total.load();
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - cur_since).count();

        if (!cur_name.empty() && ct > 0 && cd < ct && elapsed > 0.25) {
            const float target = (float)((double)cd / (double)ct);
            app.bar_shown += (target - app.bar_shown) * 0.15f;
            if (target - app.bar_shown < 0.005f) app.bar_shown = target;

            char pct[16];
            std::snprintf(pct, sizeof(pct), "%.0f%%", 100.0f * app.bar_shown);
            centered_progress(app.bar_shown, pct);

            const std::string fname = fs::path(cur_name).filename().string();
            ImGui::TextDisabled("Current: %s", fname.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", cur_name.c_str());
        } else {
            app.bar_shown = 0.0f;  // reset so the next slow file starts from 0
        }
    }

    ImGui::Separator();

    // ---- results (snapshot under a short lock, then draw without it) ----
    std::vector<Job> jobs;
    {
        std::lock_guard<std::mutex> lk(app.mtx);
        jobs = app.jobs;
    }

    // Results summary (derived from the snapshot; `status` is for main-thread
    // notices such as "no .ncm files found").
    if (jobs.empty()) {
        ImGui::TextDisabled("No results yet");
    } else {
        std::size_t ok = 0;
        for (const auto& j : jobs)
            if (j.ok) ++ok;
        const std::size_t failed = jobs.size() - ok;
        if (failed == 0)
            ImGui::TextDisabled("%zu file(s) - all OK", jobs.size());
        else
            ImGui::TextDisabled("%zu file(s) - %zu OK, %zu failed", jobs.size(), ok, failed);
    }

    float table_h = ImGui::GetContentRegionAvail().y - kPlayerBarH;
    if (table_h < 80.0f) table_h = 80.0f;

    // The results live in a bordered child window, so they get the same rounded
    // surface + outline as the player panel below (both come from the theme).
    if (ImGui::BeginChild("##results_box", ImVec2(0.0f, table_h), ImGuiChildFlags_Borders)) {
        if (ImGui::BeginTable("##results", 5,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_BordersInner,
                              ImVec2(0.0f, ImGui::GetContentRegionAvail().y))) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed, 64);
            ImGui::TableSetupColumn("Output / Error", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            int row = 0;
            for (const auto& j : jobs) {
                const int number = row + 1; // 1-based, shown in the "#" column
                ImGui::TableNextRow();
                ImGui::PushID(row++);

                const bool current = app.player.loaded() && app.player.path() == j.output;
                if (current) // now playing - rose tint, so it actually stands out
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                           ImGui::GetColorU32(rp(0xebbcba, 0.22f)));

                // Clicking a row starts that track - there is no per-row button,
                // the whole list doubles as the playlist. The row number is the
                // selectable's own label, so it sits exactly where the "#" header
                // does; drawing it after a SameLine would indent it by the
                // selectable's width.
                const bool can_play = j.ok && playable(j.format) && !j.output.empty();
                char num[16];
                std::snprintf(num, sizeof(num), "%d", number);

                ImGui::TableSetColumnIndex(0);
                ImGui::BeginDisabled(!can_play);
                if (ImGui::Selectable(num, current,
                                      ImGuiSelectableFlags_SpanAllColumns |
                                          ImGuiSelectableFlags_AllowOverlap) &&
                    app.player.load(j.output)) {
                    app.player.play();
                }
                ImGui::EndDisabled();
                if (!can_play && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip(j.ok ? "Preview supports mp3 / flac / wav only"
                                           : "Nothing to play - decryption failed");

                ImGui::TableSetColumnIndex(1);
                const std::string fname = fs::path(j.path).filename().string();
                if (current) ImGui::TextColored(rp(0xebbcba), "%s", fname.c_str());
                else ImGui::TextUnformatted(fname.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", j.path.c_str());

                ImGui::TableSetColumnIndex(2);
                if (j.ok) ImGui::TextColored(rp(0x9ccfd8), "OK");
                else ImGui::TextColored(rp(0xeb6f92), "Failed");

                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(j.format.c_str());

                ImGui::TableSetColumnIndex(4);
                if (j.ok) {
                    const std::string& label = j.title.empty() ? j.output : j.title;
                    ImGui::Text("%s  (%zu KB)", label.c_str(), j.audio_size / 1024);
                } else {
                    ImGui::TextWrapped("%s", j.message.c_str());
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    draw_player_bar(app, jobs);
    draw_browser(app);  // modal popup, drawn inside the main window's ID scope
    ImGui::End();
}

// ---------------------------------------------------------------------------

static void glfw_error_cb(int error, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, desc);
}

static App* g_app = nullptr;

static void drop_cb(GLFWwindow*, int count, const char** paths) {
    if (!g_app) return;
    std::lock_guard<std::mutex> lk(g_app->mtx);
    for (int i = 0; i < count; ++i) g_app->pending_drop.push_back(paths[i]);
}

// ---------------------------------------------------------------------------
// fonts
// ---------------------------------------------------------------------------

// Loads the bundled fonts (see assets/fonts/ and CMakeLists.txt): the primary
// face plus every fallback merged into the same ImFont with MergeMode, so
// Chinese / Korean / Russian text all render. ImGui 1.92 rasterizes glyphs on
// demand, so no glyph ranges are needed and the atlas stays small.
static void setup_fonts(float dpi_scale) {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& s = ImGui::GetStyle();

    s.FontSizeBase = 16.0f;
    s.FontScaleDpi = dpi_scale > 0.0f ? dpi_scale : 1.0f;

    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;  // points at static embedded blobs
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;

    if (!io.Fonts->AddFontFromMemoryTTF((void*)embedded_font_data(0),
                                        (int)embedded_font_size(0),
                                        s.FontSizeBase, &cfg, nullptr)) {
        io.Fonts->AddFontDefaultBitmap();
        return;
    }

    for (std::size_t i = 1; i < embedded_font_count(); ++i) {
        ImFontConfig fb = cfg;
        fb.MergeMode = true;
        io.Fonts->AddFontFromMemoryTTF((void*)embedded_font_data(i),
                                       (int)embedded_font_size(i),
                                       s.FontSizeBase, &fb, nullptr);
    }
}

int run_fonttest() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    setup_fonts(1.0f);

    ImGuiIO& io = ImGui::GetIO();
    ImFont* font = io.Fonts->Fonts.empty() ? nullptr : io.Fonts->Fonts[0];
    if (!font) {
        std::printf("FAIL: no font loaded\n");
        ImGui::DestroyContext();
        return 1;
    }

    std::printf("stored blob  : %zu bytes (compressed)\n", embedded_font_stored_size());
    for (std::size_t i = 0; i < embedded_font_count(); ++i)
        std::printf("font[%zu]      : %zu bytes%s\n", i, embedded_font_size(i),
                    i == 0 ? " (primary)" : " (merged fallback)");
    std::printf("font loaded  : yes\n");

    struct Probe {
        const char* label;
        ImWchar cp;
        bool expect;
    };
    const Probe probes[] = {
        {"ASCII   'A'", 'A', true},
        {"Chinese '中'", 0x4E2D, true},
        {"Chinese '焜'", 0x711C, true},
        {"Ext-A   '\\u3400'", 0x3400, true},
        {"Cyrillic '\\u0430'", 0x0430, true},
        {"Combining '\\u0302'", 0x0302, true},
        {"Korean  '사'", 0xC0AC, true},
    };

    int failures = 0;
    for (const auto& p : probes) {
        const bool has = font->IsGlyphInFont(p.cp);
        std::printf("  %-14s U+%04X  %s\n", p.label, (unsigned)p.cp, has ? "ok" : "missing");
        if (has != p.expect) ++failures;
    }
    std::printf("fonts loaded : %d\n", io.Fonts->Fonts.Size);

    ImGui::DestroyContext();
    return failures == 0 ? 0 : 1;
}

int run_gui(bool autotest, std::string autofile) {
    glfwSetErrorCallback(glfw_error_cb);
    if (!glfwInit()) return 1;

    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    GLFWwindow* window = glfwCreateWindow(940, 640, "ReNcm", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    float scale = xscale > 0.0f ? xscale : 1.0f;
    if (scale < 1.0f) scale = 1.0f;

    apply_rose_pine();
    ImGui::GetStyle().ScaleAllSizes(scale);
    setup_fonts(scale);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    {
        // App owns the player and the cover's GL texture, so it must be
        // destroyed while the GL context is still current - i.e. before the
        // window teardown below (Cover::~Cover calls glDeleteTextures).
        App app;
        g_app = &app;
        glfwSetWindowUserPointer(window, &app);
        glfwSetDropCallback(window, drop_cb);

        if (autotest) {
            add_input(app, autofile);
            start_jobs(app);
        }
        int auto_frames = 0;
        bool auto_second = false;

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            draw_ui(app);

            ImGui::Render();
            int w, h;
            glfwGetFramebufferSize(window, &w, &h);
            glViewport(0, 0, w, h);
            glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);

            if (autotest) {
                ++auto_frames;
                if (!auto_second && !app.running.load() && app.done.load() > 0) {
                    auto_second = true;
                    start_jobs(app);  // second run: exercises worker-thread reassignment
                } else if (auto_second && !app.running.load()) {
                    break;
                }
                if (auto_frames > 900) break;
            }
        }

        if (app.worker.joinable()) app.worker.join();
        g_app = nullptr;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

} // namespace rencm::app
