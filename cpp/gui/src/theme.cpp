// ReNcm GUI - Rose Pine theme.

#include "theme.hpp"

namespace rencm::app {

ImVec4 rp(unsigned rgb, float a) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f,
                  (rgb & 0xFF) / 255.0f, a);
}

void apply_rose_pine() {
    ImGuiStyle& s = ImGui::GetStyle();

    // One corner radius for every component - containers, buttons, inputs,
    // sliders, scrollbars, menus - so nothing looks like it came from a
    // different kit. (Selectable is left rectangular on purpose: the table row
    // highlight is a contiguous full-width band and rounded ones look broken.)
    const float round = 8.0f;
    s.WindowRounding = round;
    s.ChildRounding = round;
    s.PopupRounding = round;
    s.FrameRounding = round;
    s.ScrollbarRounding = round;
    s.GrabRounding = round;
    s.TabRounding = round;
    s.MenuItemRounding = round;
    s.DragDropTargetRounding = round;
    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize = 1.0f;
    s.WindowPadding = ImVec2(12, 12);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(8, 7);
    s.ScrollbarSize = 13.0f;

    // Rose Pine palette
    const ImVec4 base    = rp(0x191724);
    const ImVec4 surface = rp(0x1f1d2e);
    const ImVec4 overlay = rp(0x26233a);
    const ImVec4 hl_low  = rp(0x21202e);
    const ImVec4 hl_med  = rp(0x403d52);
    const ImVec4 hl_high = rp(0x524f67);
    const ImVec4 muted   = rp(0x6e6a86);
    const ImVec4 text    = rp(0xe0def4);
    const ImVec4 love    = rp(0xeb6f92);
    const ImVec4 rose    = rp(0xebbcba);
    const ImVec4 pine    = rp(0x31748f);
    const ImVec4 foam    = rp(0x9ccfd8);
    const ImVec4 iris    = rp(0xc4a7e7);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = muted;
    c[ImGuiCol_WindowBg] = base;
    c[ImGuiCol_ChildBg] = surface;
    c[ImGuiCol_PopupBg] = surface;
    c[ImGuiCol_Border] = hl_med;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = overlay;
    c[ImGuiCol_FrameBgHovered] = hl_low;
    c[ImGuiCol_FrameBgActive] = hl_med;
    c[ImGuiCol_TitleBg] = surface;
    c[ImGuiCol_TitleBgActive] = overlay;
    c[ImGuiCol_TitleBgCollapsed] = base;
    c[ImGuiCol_MenuBarBg] = surface;
    c[ImGuiCol_ScrollbarBg] = base;
    c[ImGuiCol_ScrollbarGrab] = hl_med;
    c[ImGuiCol_ScrollbarGrabHovered] = hl_high;
    c[ImGuiCol_ScrollbarGrabActive] = iris;
    c[ImGuiCol_CheckMark] = rose;
    c[ImGuiCol_SliderGrab] = iris;
    c[ImGuiCol_SliderGrabActive] = pine;
    c[ImGuiCol_Button] = overlay;
    c[ImGuiCol_ButtonHovered] = hl_med;
    c[ImGuiCol_ButtonActive] = pine;
    c[ImGuiCol_Header] = overlay;
    c[ImGuiCol_HeaderHovered] = hl_med;
    c[ImGuiCol_HeaderActive] = pine;
    c[ImGuiCol_Separator] = hl_med;
    c[ImGuiCol_SeparatorHovered] = iris;
    c[ImGuiCol_SeparatorActive] = iris;
    c[ImGuiCol_ResizeGrip] = hl_med;
    c[ImGuiCol_ResizeGripHovered] = iris;
    c[ImGuiCol_ResizeGripActive] = pine;
    c[ImGuiCol_Tab] = surface;
    c[ImGuiCol_TabHovered] = hl_med;
    c[ImGuiCol_TabSelected] = pine;
    c[ImGuiCol_TabSelectedOverline] = rose;
    c[ImGuiCol_TabDimmed] = base;
    c[ImGuiCol_TabDimmedSelected] = overlay;
    c[ImGuiCol_PlotLines] = foam;
    c[ImGuiCol_PlotLinesHovered] = love;
    c[ImGuiCol_PlotHistogram] = rose;
    c[ImGuiCol_PlotHistogramHovered] = rose;
    c[ImGuiCol_TableHeaderBg] = surface;
    c[ImGuiCol_TableBorderStrong] = hl_med;
    c[ImGuiCol_TableBorderLight] = hl_low;
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = rp(0xe0def4, 0.025f);
    c[ImGuiCol_TextSelectedBg] = hl_med;
    c[ImGuiCol_NavCursor] = rose;
    c[ImGuiCol_DragDropTarget] = rose;
    c[ImGuiCol_ModalWindowDimBg] = rp(0x191724, 0.68f);
}

} // namespace rencm::app
