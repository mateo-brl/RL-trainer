#include "plugin/ui/Ui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "imgui.h"

namespace rlt {

// ---- UiContext -------------------------------------------------------------------------------------------
void UiContext::setCvar(const std::string& name, const std::string& value) const {
    CVarWrapper cv = cvars->getCvar(name);
    if (cv.IsNull()) return;
    cv.setValue(value);
    if (configDirty) *configDirty = true;
}

void UiContext::post(const std::string& module, const std::string& name, std::vector<std::string> args) const {
    Command c;
    c.module = module;
    c.name = name;
    c.args = std::move(args);
    engine.post(std::move(c));
}

void UiContext::runOnGame(std::function<void()> fn) const {
    // Runs a frame later: skip it if the plugin unloaded in between, and never let an exception reach the game.
    game->Execute([alive = this->alive, fn = std::move(fn)](GameWrapper*) {
        if (alive && !*alive) return;
        try {
            fn();
        } catch (...) {
        }
    });
}

Settings UiContext::settings() const {
    auto lk = engine.lock();
    return engine.settings();
}

// ---- Theme -----------------------------------------------------------------------------------------------
namespace {

ImVec4 rgb(unsigned hex, float alpha = 1.f) {
    return ImVec4(static_cast<float>((hex >> 16) & 0xFF) / 255.f, static_cast<float>((hex >> 8) & 0xFF) / 255.f,
                  static_cast<float>(hex & 0xFF) / 255.f, alpha);
}

// One accent colour: the amber of every HUD panel title and of the section titles of this menu, so the menu and
// the HUD read as one product. Everything else is neutral; the amber only marks what is selected, checked, grabbed,
// hovered or pressed. Text never sits on the bright amber itself, only on its dark shades.
// WCAG contrast ratios (computed with the WCAG 2 formula):
//   Text #ECEDEF: 15.2:1 on the background #16181C, 13.8:1 on the sidebar and popups #1E2126, 12.1:1 on fields
//   and headers #272B32, 11.3:1 on buttons #2B3038, 10.9:1 on the selected item #3A3120, 6.0:1 on a hovered
//   button #6B5622, 5.0:1 on a pressed button #7A6226.
//   TextDisabled #9BA1AB: 6.8:1 on the background, 6.2:1 on the sidebar and popups, 5.5:1 on fields and headers.
//   Amber #FFC740: 11.4:1 on the background, 8.2:1 on the selected item, 9.1:1 on fields (check marks).
constexpr unsigned kAccent = 0xFFC740;
constexpr unsigned kAccentLight = 0xFFD978;   // grab being dragged
constexpr unsigned kAccentHover = 0x6B5622;   // hovered button
constexpr unsigned kAccentActive = 0x7A6226;  // pressed button
constexpr unsigned kSelectedBg = 0x3A3120;    // selected sidebar item, selected filter
constexpr unsigned kBackground = 0x16181C;
constexpr unsigned kSidebar = 0x1E2126;
constexpr unsigned kField = 0x272B32;
constexpr unsigned kFieldHover = 0x30353D;
constexpr unsigned kFieldActive = 0x383E47;
constexpr unsigned kButton = 0x2B3038;
constexpr unsigned kLine = 0x2E3238;
constexpr float kSidebarWidth = 180.f;

struct ThemeColor {
    ImGuiCol idx;
    ImVec4 value;
};

struct ThemeVar {
    ImGuiStyleVar idx;
    ImVec2 value;
    bool isVec;
};

const std::vector<ThemeColor>& themeColors() {
    static const std::vector<ThemeColor> colors = {
        {ImGuiCol_Text, rgb(0xECEDEF)},
        {ImGuiCol_TextDisabled, rgb(0x9BA1AB)},
        {ImGuiCol_WindowBg, rgb(kBackground)},
        {ImGuiCol_ChildBg, rgb(kBackground)},
        {ImGuiCol_PopupBg, rgb(kSidebar)},
        {ImGuiCol_Border, rgb(kLine)},
        {ImGuiCol_BorderShadow, rgb(0x000000, 0.f)},
        {ImGuiCol_FrameBg, rgb(kField)},
        {ImGuiCol_FrameBgHovered, rgb(kFieldHover)},
        {ImGuiCol_FrameBgActive, rgb(kFieldActive)},
        {ImGuiCol_TitleBg, rgb(0x111316)},
        {ImGuiCol_TitleBgActive, rgb(0x1A1D22)},
        {ImGuiCol_TitleBgCollapsed, rgb(0x111316)},
        {ImGuiCol_MenuBarBg, rgb(0x1A1D22)},
        {ImGuiCol_ScrollbarBg, rgb(kBackground, 0.f)},
        {ImGuiCol_ScrollbarGrab, rgb(0x3A3F48)},
        {ImGuiCol_ScrollbarGrabHovered, rgb(0x4A505A)},
        {ImGuiCol_ScrollbarGrabActive, rgb(0x5A616C)},
        {ImGuiCol_CheckMark, rgb(kAccent)},
        {ImGuiCol_RangeSliderBar, rgb(kAccent)},  // BakkesMod's own ImGui addition
        {ImGuiCol_SliderGrab, rgb(kAccent)},
        {ImGuiCol_SliderGrabActive, rgb(kAccentLight)},
        {ImGuiCol_Button, rgb(kButton)},
        {ImGuiCol_ButtonHovered, rgb(kAccentHover)},
        {ImGuiCol_ButtonActive, rgb(kAccentActive)},
        {ImGuiCol_Header, rgb(kField)},
        {ImGuiCol_HeaderHovered, rgb(kFieldHover)},
        {ImGuiCol_HeaderActive, rgb(kFieldActive)},
        {ImGuiCol_Separator, rgb(kLine)},
        {ImGuiCol_SeparatorHovered, rgb(kAccentHover)},
        {ImGuiCol_SeparatorActive, rgb(kAccentActive)},
        {ImGuiCol_ResizeGrip, rgb(0x3A3F48, 0.6f)},
        {ImGuiCol_ResizeGripHovered, rgb(kAccentHover)},
        {ImGuiCol_ResizeGripActive, rgb(kAccentActive)},
        {ImGuiCol_Tab, rgb(0x22252B)},
        {ImGuiCol_TabHovered, rgb(kFieldHover)},
        {ImGuiCol_TabActive, rgb(0x353A43)},
        {ImGuiCol_TabUnfocused, rgb(0x22252B)},
        {ImGuiCol_TabUnfocusedActive, rgb(0x2E333B)},
        {ImGuiCol_PlotLines, rgb(0x9BA1AB)},
        {ImGuiCol_PlotLinesHovered, rgb(kAccent)},
        {ImGuiCol_PlotHistogram, rgb(kAccent)},
        {ImGuiCol_PlotHistogramHovered, rgb(kAccentLight)},
        {ImGuiCol_TextSelectedBg, rgb(kAccent, 0.3f)},
        {ImGuiCol_DragDropTarget, rgb(kAccent)},
        {ImGuiCol_NavHighlight, rgb(kAccent)},
        {ImGuiCol_NavWindowingHighlight, rgb(kAccent, 0.7f)},
        {ImGuiCol_NavWindowingDimBg, rgb(0x000000, 0.5f)},
        {ImGuiCol_ModalWindowDimBg, rgb(0x000000, 0.55f)},
    };
    return colors;
}

const std::vector<ThemeVar>& themeVars() {
    static const std::vector<ThemeVar> vars = {
        {ImGuiStyleVar_WindowRounding, ImVec2(8.f, 0.f), false},
        {ImGuiStyleVar_WindowPadding, ImVec2(10.f, 10.f), true},
        {ImGuiStyleVar_ChildRounding, ImVec2(6.f, 0.f), false},
        {ImGuiStyleVar_PopupRounding, ImVec2(6.f, 0.f), false},
        {ImGuiStyleVar_FramePadding, ImVec2(8.f, 4.f), true},
        {ImGuiStyleVar_FrameRounding, ImVec2(5.f, 0.f), false},
        {ImGuiStyleVar_ItemSpacing, ImVec2(8.f, 6.f), true},
        {ImGuiStyleVar_ItemInnerSpacing, ImVec2(6.f, 4.f), true},
        {ImGuiStyleVar_ScrollbarSize, ImVec2(12.f, 0.f), false},
        {ImGuiStyleVar_ScrollbarRounding, ImVec2(6.f, 0.f), false},
        {ImGuiStyleVar_GrabMinSize, ImVec2(12.f, 0.f), false},
        {ImGuiStyleVar_GrabRounding, ImVec2(4.f, 0.f), false},
        {ImGuiStyleVar_TabRounding, ImVec2(5.f, 0.f), false},
    };
    return vars;
}

const char* g_requestedPage = nullptr;  // render thread only (ui::goToPage)

} // namespace

// ---- Widgets ---------------------------------------------------------------------------------------------
namespace ui {

void pushTheme() {
    for (const ThemeColor& c : themeColors()) ImGui::PushStyleColor(c.idx, c.value);
    for (const ThemeVar& v : themeVars()) {
        if (v.isVec) ImGui::PushStyleVar(v.idx, v.value);
        else ImGui::PushStyleVar(v.idx, v.value.x);
    }
}

void popTheme() {
    ImGui::PopStyleVar(static_cast<int>(themeVars().size()));
    ImGui::PopStyleColor(static_cast<int>(themeColors().size()));
}

ImVec4 accentColor() { return rgb(kAccent); }
ImVec4 accentSelectedBg() { return rgb(kSelectedBg); }

void goToPage(const char* id) { g_requestedPage = id; }

void progressBar(float fraction01, const std::string& value, float width) {
    const float valueWidth =
        value.empty() ? 0.f : ImGui::CalcTextSize(value.c_str()).x + ImGui::GetStyle().ItemSpacing.x;
    float w = width > 0.f ? width : ImGui::GetContentRegionAvail().x + width - valueWidth;
    w = std::max(w, 40.f);
    ImGui::ProgressBar(std::clamp(fraction01, 0.f, 1.f), ImVec2(w, 0.f), "");  // "" = no text on the bar
    if (!value.empty()) {
        ImGui::SameLine();
        ImGui::TextUnformatted(value.c_str());
    }
}

void helpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void textColored(const Color& c, const std::string& text) {
    ImGui::TextColored(ImVec4(c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f), "%s", text.c_str());
}

void separatorText(const std::string& text) {
    ImGui::Spacing();
    ImGui::TextColored(accentColor(), "%s", text.c_str());
    ImGui::Separator();
}

std::string percent(float ratio01) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d %%", static_cast<int>(ratio01 * 100.f + 0.5f));
    return buf;
}

static void tooltip(const SettingDef& def) {
    if (!def.tooltip.empty() && ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.f);
        ImGui::TextUnformatted(def.tooltip.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool settingWidget(const UiContext& ctx, const SettingDef& def, const Settings& current) {
    const std::string label = def.label + "##" + def.cvar;
    const std::string value = def.read ? def.read(current) : def.defaultValue;
    std::string newValue;
    bool changed = false;
    switch (def.type) {
    case SettingType::Bool: {
        bool b = false;
        parseBoolValue(value, b);
        if ((changed = ImGui::Checkbox(label.c_str(), &b))) newValue = boolToCvar(b);
        break;
    }
    case SettingType::Int: {
        int i = 0;
        parseIntValue(value, i);
        if ((changed = ImGui::SliderInt(label.c_str(), &i, static_cast<int>(def.minValue), static_cast<int>(def.maxValue))))
            newValue = intToCvar(i);
        break;
    }
    case SettingType::Float: {
        float f = 0.f;
        parseFloatValue(value, f);
        char fmt[16];
        std::snprintf(fmt, sizeof(fmt), "%%.%df", std::clamp(def.decimals, 0, 4));
        if ((changed = ImGui::SliderFloat(label.c_str(), &f, def.minValue, def.maxValue, fmt))) newValue = floatToCvar(f);
        break;
    }
    case SettingType::Color: {
        Color c;
        parseColor(value, c);
        float col[4] = {c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f};
        if ((changed = ImGui::ColorEdit4(label.c_str(), col, ImGuiColorEditFlags_AlphaBar)))
            newValue = toHex(Color(col[0] * 255.f, col[1] * 255.f, col[2] * 255.f, col[3] * 255.f));
        break;
    }
    case SettingType::Choice: {
        int i = 0;
        parseIntValue(value, i);
        const int n = static_cast<int>(def.choices.size());
        if (n == 0) break;
        i = std::clamp(i, 0, n - 1);
        if (ImGui::BeginCombo(label.c_str(), def.choices[static_cast<size_t>(i)].c_str())) {
            for (int k = 0; k < n; ++k) {
                const bool selected = k == i;
                if (ImGui::Selectable(def.choices[static_cast<size_t>(k)].c_str(), selected)) {
                    changed = true;
                    newValue = intToCvar(k);
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        break;
    }
    case SettingType::Text: {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%s", value.c_str());
        if ((changed = ImGui::InputText(label.c_str(), buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue)))
            newValue = buf;
        break;
    }
    }
    tooltip(def);
    if (changed) ctx.setCvar(def.cvar, newValue);
    return changed;
}

void resetToDefaults(const UiContext& ctx, const std::string& module, const std::string& section) {
    for (const SettingDef* d : ctx.engine.schema().forModule(module)) {
        if (!section.empty() && d->section != section) continue;
        ctx.setCvar(d->cvar, d->defaultValue);
    }
}

void resetCvars(const UiContext& ctx, const std::vector<std::string>& cvars) {
    for (const std::string& name : cvars)
        if (const SettingDef* d = ctx.engine.schema().find(name)) ctx.setCvar(d->cvar, d->defaultValue);
}

void settingsSection(const UiContext& ctx, const std::string& module, const std::string& section, bool withReset) {
    const Settings current = ctx.settings();
    bool any = false;
    for (const SettingDef* d : ctx.engine.schema().forModule(module)) {
        if (d->section != section) continue;
        if (d->advanced && !ctx.showAdvanced) continue;
        settingWidget(ctx, *d, current);
        any = true;
    }
    if (any && withReset) {
        const std::string id = "Réinitialiser##" + module + section;
        if (ImGui::SmallButton(id.c_str())) resetToDefaults(ctx, module, section);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remet les réglages de cette section par défaut.");
    }
}

void moduleSettings(const UiContext& ctx, const std::string& module) {
    const auto sections = ctx.engine.schema().sections(module);
    bool first = true;
    for (const auto& s : sections) {
        // Skip sections that only contain advanced entries when advanced mode is off.
        bool visible = false;
        for (const SettingDef* d : ctx.engine.schema().forModule(module))
            if (d->section == s && (!d->advanced || ctx.showAdvanced)) visible = true;
        if (!visible) continue;
        const std::string header = s + "##" + module;
        if (ImGui::CollapsingHeader(header.c_str(), first ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
            ImGui::Indent();
            settingsSection(ctx, module, s);
            ImGui::Unindent();
        }
        first = false;
    }
}

} // namespace ui

// ---- Pages ------------------------------------------------------------------------------------------------
const std::vector<UiTab>& uiTabs() {
    static const std::vector<UiTab> tabs = {
        {"home", "Accueil", &renderHomeTab, ""},
        {"hud", "HUD", &renderHudTab, ""},
        {"firsttouch", "Première touche", &renderFirstTouchTab, "Entraînement"},
        {"mechanics", "Mécas mouvement", &renderMechanicsTab, "Entraînement"},
        {"ballmech", "Mécas balle", &renderBallMechTab, "Entraînement"},
        {"kickoff", "Kickoff", &renderKickoffTab, "Entraînement"},
        {"courses", "Parcours", &renderCoursesTab, "Entraînement"},
        {"shooting", "Tirs & flicks", &renderShootingTab, "Entraînement"},
        {"maps", "Maps", &renderMapsTab, "Entraînement"},
        {"overlay", "Écart de vitesse", &renderOverlayTab, "Outils"},
        {"prediction", "Prédiction", &renderPredictionTab, "Outils"},
        {"coach", "Progression", &renderCoachTab, "Outils"},
        {"profile", "Ma config", &renderProfileTab, "Outils"},
        {"general", "Réglages", &renderGeneralTab, "Outils"},
    };
    return tabs;
}

namespace {

// One sidebar entry: full-width row, rounded background when hovered or selected, amber bar and amber text
// when selected.
bool sidebarItem(const UiTab& tab, bool selected) {
    const float rowHeight = ImGui::GetTextLineHeight() + 10.f;
    ImGui::PushID(tab.id);
    // The row background is drawn below: the Selectable's own square highlight stays invisible.
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.f, 0.f, 0.f, 0.f));
    const bool clicked = ImGui::Selectable("##page", selected, 0, ImVec2(0.f, rowHeight));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopStyleColor(3);
    ImGui::PopID();

    // Same 6 px margin on both sides of the row (the Selectable itself reaches the right edge of the sidebar).
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    const float windowX = ImGui::GetWindowPos().x;
    const ImVec2 top(windowX + ImGui::GetWindowContentRegionMin().x - 4.f, a.y + 1.f);
    const ImVec2 bottom(windowX + ImGui::GetWindowContentRegionMax().x + 4.f, b.y - 1.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected) {
        dl->AddRectFilled(top, bottom, ImGui::GetColorU32(rgb(kSelectedBg)), 5.f);
        dl->AddRectFilled(top, ImVec2(top.x + 3.f, bottom.y), ImGui::GetColorU32(rgb(kAccent)), 1.5f);
    } else if (hovered) {
        dl->AddRectFilled(top, bottom, ImGui::GetColorU32(rgb(kButton)), 5.f);
    }
    const ImVec4 textColor = selected ? rgb(kAccent) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
    dl->AddText(ImVec2(top.x + 12.f, a.y + (b.y - a.y - ImGui::GetTextLineHeight()) * 0.5f),
                ImGui::GetColorU32(textColor), tab.title);
    return clicked;
}

} // namespace

void renderUi(UiContext& ctx) {
    const auto& tabs = uiTabs();
    if (tabs.empty()) return;
    ui::pushTheme();

    // Selected page, kept in the ImGui state of the window that hosts the menu (F2 page or our own window).
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID pageKey = ImGui::GetID("##rlt_selected_page");
    int page = storage->GetInt(pageKey, 0);
    if (g_requestedPage) {
        for (size_t i = 0; i < tabs.size(); ++i)
            if (std::strcmp(tabs[i].id, g_requestedPage) == 0) page = static_cast<int>(i);
        g_requestedPage = nullptr;
    }
    page = std::clamp(page, 0, static_cast<int>(tabs.size()) - 1);

    const float height = std::max(ImGui::GetContentRegionAvail().y, 380.f);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, rgb(kSidebar));
    ImGui::BeginChild("##rlt_sidebar", ImVec2(kSidebarWidth, height), false, ImGuiWindowFlags_AlwaysUseWindowPadding);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.f, 2.f));  // rows close together: no scroll at 480 px
    int clicked = page;
    const char* group = "";
    for (size_t i = 0; i < tabs.size(); ++i) {
        const UiTab& t = tabs[i];
        if (std::strcmp(t.group, group) != 0) {
            group = t.group;
            if (*group) {
                ImGui::Dummy(ImVec2(0.f, 8.f));
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.f);
                ImGui::TextDisabled("%s", group);
                ImGui::Dummy(ImVec2(0.f, 2.f));
            }
        }
        if (sidebarItem(t, static_cast<int>(i) == page)) clicked = static_cast<int>(i);
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleColor();  // after EndChild: ImGui checks the colour stack when the child ends
    page = clicked;
    storage->SetInt(pageKey, page);

    // One child per page, so that each page keeps its own scroll position.
    const UiTab& current = tabs[static_cast<size_t>(page)];
    const std::string pageId = std::string("##rlt_page_") + current.id;
    ImGui::SameLine();
    ImGui::BeginChild(pageId.c_str(), ImVec2(0.f, height), false, ImGuiWindowFlags_AlwaysUseWindowPadding);
    current.render(ctx);
    ImGui::EndChild();

    ui::popTheme();
}

} // namespace rlt
