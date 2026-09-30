// ReNcm GUI - bottom player bar (transport, seek, volume, visualizer).

#include "app.hpp"

#include "theme.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace rencm::app {

namespace fs = std::filesystem;

// Seconds as m:ss.
static std::string time_str(double sec) {
    if (!std::isfinite(sec) || sec < 0.0) sec = 0.0;
    const int total = (int)(sec + 0.5);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%02d", total / 60, total % 60);
    return buf;
}

// Bottom player bar: playlist transport, seek, volume and the visualizer.
//
// Playback follows `jobs` in table order, so the bar doubles as a playlist:
// Prev/Next walk it, and a finished track advances on its own.
void draw_player_bar(App& app, const std::vector<Job>& jobs) {
    Player& p = app.player;
    const float kBtn = 128.0f;  // same width as the buttons above

    std::vector<std::string> tracks;
    for (const auto& j : jobs)
        if (j.ok && playable(j.format) && !j.output.empty()) tracks.push_back(j.output);

    auto index_of = [&tracks](const std::string& path) {
        for (std::size_t i = 0; i < tracks.size(); ++i)
            if (tracks[i] == path) return static_cast<int>(i);
        return -1;
    };
    int cur = -1;
    if (p.loaded()) cur = index_of(p.path());
    auto goto_track = [&](int i) {
        if (i >= 0 && i < static_cast<int>(tracks.size()) && p.load(tracks[i])) p.play();
    };

    // A finished track hands over to the next one; the end of the list rewinds.
    if (p.ended()) {
        if (cur >= 0 && cur + 1 < static_cast<int>(tracks.size()))
            goto_track(cur + 1);
        else {
            p.pause();
            p.seek(0.0);
        }
    }

    p.update_spectrum();

    // The bar is its own rounded panel, like a music player strip. Layout:
    // cover art on the left, then title | transport | volume, the clocks
    // bracketing the seek bar underneath, and the visualiser filling the rest.
    const bool has = p.loaded();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float art = 96.0f;     // cover thumbnail
    const float strip_h = 34.0f; // visualiser
    const float vol_w = 132.0f;
    const float t_w = 46.0f; // fixed slot for each clock label
    const float pad = 12.0f; // panel inset

    ImGui::Dummy(ImVec2(0.0f, 6.0f)); // a little air above the panel

    const float row_x = ImGui::GetCursorPosX();
    const ImVec2 bar_pos = ImGui::GetCursorScreenPos();
    // `right` is the results table's right edge, so the panel box lines up with
    // the table on both sides; the contents then sit `pad` inside the border.
    const float right = row_x + ImGui::GetContentRegionAvail().x;
    const float inner_x = row_x + pad;
    const float inner_right = right - pad;
    const float content_x = inner_x + art + gap;
    const float content_w = inner_right - content_x;

    // Channels merge in order, so channel 0 ends up behind channel 1. The
    // widgets are submitted first (channel 1) and the panel afterwards but into
    // channel 0, which lets it be sized from the measured content.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);

    // ---- row 1: title | transport | volume --------------------------------
    ImGui::SetCursorPosX(content_x);
    if (has) {
        const std::string name = fs::path(p.path()).filename().string();
        ImGui::TextUnformatted(name.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.path().c_str());
    } else if (!p.error().empty()) {
        ImGui::TextDisabled("%s", p.error().c_str());
    } else {
        ImGui::TextDisabled("nothing loaded");
    }

    const float btns_w = 3.0f * kBtn + 2.0f * gap;
    ImGui::SameLine(content_x + std::max(0.0f, (content_w - btns_w) * 0.5f));

    ImGui::BeginDisabled(!has || cur <= 0);
    if (ImGui::Button("Prev", ImVec2(kBtn, 0))) goto_track(cur - 1);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!has);
    if (ImGui::Button(p.playing() ? "Pause" : "Play", ImVec2(kBtn, 0))) p.toggle();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!has || cur < 0 || cur + 1 >= static_cast<int>(tracks.size()));
    if (ImGui::Button("Next", ImVec2(kBtn, 0))) goto_track(cur + 1);
    ImGui::EndDisabled();

    float vol = p.volume() * 100.0f;
    ImGui::SameLine(inner_right - vol_w);
    ImGui::SetNextItemWidth(vol_w - 52.0f);
    ImGui::BeginDisabled(!has);
    if (ImGui::SliderFloat("##vol", &vol, 0.0f, 100.0f, "")) p.set_volume(vol / 100.0f);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("%.0f%%", vol);

    // ---- row 2: elapsed | seek | total ------------------------------------
    const double dur = p.duration();
    const double pos = p.position();

    ImGui::SetCursorPosX(content_x);
    ImGui::TextUnformatted(time_str(pos).c_str());

    ImGui::SameLine(content_x + t_w);
    float spos = static_cast<float>(pos);
    ImGui::SetNextItemWidth(std::max(80.0f, content_w - 2.0f * t_w - 2.0f * gap));
    ImGui::BeginDisabled(!has || dur <= 0.0);
    if (ImGui::SliderFloat("##seek", &spos, 0.0f, dur > 0.0 ? static_cast<float>(dur) : 1.0f, ""))
        p.seek(spos);
    ImGui::EndDisabled();

    ImGui::SameLine(inner_right - t_w);
    ImGui::TextUnformatted(time_str(dur).c_str());

    // ---- visualiser -------------------------------------------------------
    const float* spec = p.spectrum();
    ImGui::SetCursorPosX(content_x);
    const ImVec2 strip_pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##spectrum", ImVec2(content_w, strip_h));
    if (content_w > 1.0f) {
        dl->AddRectFilled(strip_pos, ImVec2(strip_pos.x + content_w, strip_pos.y + strip_h),
                          ImGui::GetColorU32(rp(0x191724)), 5.0f); // recessed trough
        const ImU32 bar_lo = ImGui::GetColorU32(rp(0xebbcba));         // rose
        const ImU32 bar_hi = ImGui::GetColorU32(rp(0xeb6f92));         // love
        const float bw = content_w / static_cast<float>(kSpectrumBins);
        const float inset = std::min(1.5f, bw * 0.2f);
        for (int b = 0; b < kSpectrumBins; ++b) {
            const float bh = std::max(2.0f, spec[b] * (strip_h - 6.0f));
            const ImVec2 top(strip_pos.x + b * bw + inset, strip_pos.y + strip_h - bh - 3.0f);
            const ImVec2 bot(strip_pos.x + (b + 1) * bw - inset, strip_pos.y + strip_h - 3.0f);
            dl->AddRectFilledMultiColor(top, bot, bar_hi, bar_hi, bar_lo, bar_lo);
        }
    }

    // ---- panel behind everything, sized to the content --------------------
    const float panel_top = bar_pos.y - pad;
    const float panel_bot = strip_pos.y + strip_h + pad;

    // Same radius and outline as the results table box, taken from the theme so
    // the two can never drift apart.
    const float round = ImGui::GetStyle().ChildRounding;
    dl->ChannelsSetCurrent(0);
    const ImVec2 panel_min(row_x, panel_top);
    const ImVec2 panel_max(right, panel_bot);
    dl->AddRectFilled(panel_min, panel_max, ImGui::GetColorU32(rp(0x1f1d2e)), round);
    dl->AddRect(panel_min, panel_max, ImGui::GetColorU32(ImGuiCol_Border), round, 0, 1.0f);
    dl->ChannelsMerge();

    // ---- cover art, vertically centred in the panel ----------------------
    const unsigned int cover_tex = app.cover.get(has ? p.path() : std::string{});
    const float art_y = (panel_top + panel_bot - art) * 0.5f;
    const ImVec2 art_min(inner_x, art_y);
    const ImVec2 art_max(inner_x + art, art_y + art);
    if (cover_tex != 0) {
        dl->AddImageRounded((ImTextureID)cover_tex, art_min, art_max, ImVec2(0, 0), ImVec2(1, 1),
                            IM_COL32_WHITE, round);
    } else {
        dl->AddRectFilled(art_min, art_max, ImGui::GetColorU32(rp(0x26233a)), round);
        // Distinguish "this track simply has no cover" from "nothing is loaded" -
        // otherwise a playing, coverless track looks identical to an idle bar.
        if (has) {
            const char* hint = "no cover";
            const ImVec2 ts = ImGui::CalcTextSize(hint);
            dl->AddText(ImVec2(art_min.x + (art - ts.x) * 0.5f, art_min.y + (art - ts.y) * 0.5f),
                        ImGui::GetColorU32(rp(0x6e6a86)), hint); // muted
        }
    }
}

} // namespace rencm::app
