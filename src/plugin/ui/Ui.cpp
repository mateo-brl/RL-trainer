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
    game->Execute([fn = std::move(fn)](GameWrapper*) { fn(); });
}

Settings UiContext::settings() const {
    auto lk = engine.lock();
    return engine.settings();
}

// ---- Widgets ---------------------------------------------------------------------------------------------
namespace ui {

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
    ImGui::TextColored(ImVec4(1.f, 0.78f, 0.25f, 1.f), "%s", text.c_str());
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

void settingWidget(const UiContext& ctx, const SettingDef& def, const Settings& current) {
    const std::string label = def.label + "##" + def.cvar;
    const std::string value = def.read ? def.read(current) : def.defaultValue;
    switch (def.type) {
    case SettingType::Bool: {
        bool b = false;
        parseBoolValue(value, b);
        if (ImGui::Checkbox(label.c_str(), &b)) ctx.setCvar(def.cvar, boolToCvar(b));
        break;
    }
    case SettingType::Int: {
        int i = 0;
        parseIntValue(value, i);
        if (ImGui::SliderInt(label.c_str(), &i, static_cast<int>(def.minValue), static_cast<int>(def.maxValue)))
            ctx.setCvar(def.cvar, intToCvar(i));
        break;
    }
    case SettingType::Float: {
        float f = 0.f;
        parseFloatValue(value, f);
        char fmt[16];
        std::snprintf(fmt, sizeof(fmt), "%%.%df", std::clamp(def.decimals, 0, 4));
        if (ImGui::SliderFloat(label.c_str(), &f, def.minValue, def.maxValue, fmt)) ctx.setCvar(def.cvar, floatToCvar(f));
        break;
    }
    case SettingType::Color: {
        Color c;
        parseColor(value, c);
        float col[4] = {c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f};
        if (ImGui::ColorEdit4(label.c_str(), col, ImGuiColorEditFlags_AlphaBar))
            ctx.setCvar(def.cvar, toHex(Color(col[0] * 255.f, col[1] * 255.f, col[2] * 255.f, col[3] * 255.f)));
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
                if (ImGui::Selectable(def.choices[static_cast<size_t>(k)].c_str(), selected)) ctx.setCvar(def.cvar, intToCvar(k));
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        break;
    }
    case SettingType::Text: {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%s", value.c_str());
        if (ImGui::InputText(label.c_str(), buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
            ctx.setCvar(def.cvar, buf);
        break;
    }
    }
    tooltip(def);
}

void resetToDefaults(const UiContext& ctx, const std::string& module, const std::string& section) {
    for (const SettingDef* d : ctx.engine.schema().forModule(module)) {
        if (!section.empty() && d->section != section) continue;
        ctx.setCvar(d->cvar, d->defaultValue);
    }
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

// ---- Tabs ------------------------------------------------------------------------------------------------
const std::vector<UiTab>& uiTabs() {
    static const std::vector<UiTab> tabs = {
        {"home", "Accueil", &renderHomeTab},
        {"overlay", "Affichage", &renderOverlayTab},
        {"firsttouch", "Première touche", &renderFirstTouchTab},
        {"mechanics", "Mécaniques", &renderMechanicsTab},
        {"kickoff", "Kickoff", &renderKickoffTab},
        {"courses", "Parcours", &renderCoursesTab},
        {"prediction", "Prédiction", &renderPredictionTab},
        {"maps", "Maps", &renderMapsTab},
        {"coach", "Progression", &renderCoachTab},
        {"profile", "Ma config", &renderProfileTab},
        {"general", "Réglages", &renderGeneralTab},
    };
    return tabs;
}

void renderUi(UiContext& ctx) {
    if (ImGui::BeginTabBar("##rlt_tabs")) {
        for (const auto& t : uiTabs()) {
            if (ImGui::BeginTabItem(t.title)) {
                ImGui::BeginChild("##rlt_tab_content", ImVec2(0, 0), false);
                t.render(ctx);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
}

} // namespace rlt
