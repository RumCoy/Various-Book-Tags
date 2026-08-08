#include "Menu.h"

#include "BookProcessor.h"
#include "Config.h"

#include <SKSEMenuFramework.h>

#include <utility>

namespace VariousBookTags::Menu
{
    namespace
    {
        struct MenuSettings
        {
            bool enabled{};
            bool skillTags{};
            bool modNameTags{};
            bool fallback{};
        };

        void QueueSettingsUpdate(MenuSettingsUpdate update)
        {
            auto* tasks = SKSE::GetTaskInterface();
            if (!tasks) {
                SKSE::log::error("Task interface unavailable");
                return;
            }

            tasks->AddTask([update = std::move(update)]() {
                auto& config = Config::GetSingleton();
                if (!config.ApplyMenuSettings(update)) {
                    SKSE::log::error("Failed to persist one or more menu setting files");
                }
                BookProcessor::Apply();
            });
        }

        void __stdcall RenderSettings()
        {
            static MenuSettings settings;
            static bool initialized = false;
            if (!initialized) {
                const auto& config = Config::GetSingleton();
                settings.enabled = config.Enabled();
                settings.skillTags = config.SkillTagsEnabled();
                settings.modNameTags = config.ModNameTagsEnabled();
                settings.fallback = config.GlobalPluginNameFallbackEnabled();
                initialized = true;
            }

            MenuSettingsUpdate update;
            if (ImGuiMCP::Checkbox("Enable Various Book Tags", &settings.enabled)) {
                update.enabled = settings.enabled;
            }
            if (ImGuiMCP::Checkbox(
                    "Enable skill and spell-school tags", &settings.skillTags)) {
                update.skillTags = settings.skillTags;
            }
            if (ImGuiMCP::IsItemHovered(0)) {
                ImGuiMCP::SetTooltip(
                    "DEFAULT: ON. Tags such as (Restoration) or (Archery)");
            }
            if (ImGuiMCP::Checkbox("Enable mod-name tags", &settings.modNameTags)) {
                update.modNameTags = settings.modNameTags;
            }
            if (ImGuiMCP::IsItemHovered(0)) {
                ImGuiMCP::SetTooltip(
                    "DEFAULT: ON. Tags such as (Little Library) or (Immersive College)");
            }
            if (ImGuiMCP::Checkbox(
                    "Tag unconfigured mods with plugin names", &settings.fallback)) {
                update.globalPluginNameFallback = settings.fallback;
            }
            if (ImGuiMCP::IsItemHovered(0)) {
                ImGuiMCP::SetTooltip(
                    "DEFAULT: OFF. Uses the plugin filename as the tag for books from "
                    "unconfigured non-vanilla plugins.");
            }
            if (update.enabled.has_value() || update.skillTags.has_value() ||
                update.modNameTags.has_value() ||
                update.globalPluginNameFallback.has_value()) {
                QueueSettingsUpdate(std::move(update));
            }
        }
    }

    void Register()
    {
        static bool registered = false;
        if (registered) {
            return;
        }

        if (!SKSEMenuFramework::IsInstalled()) {
            return;
        }

        if (SKSEMenuFramework::GetMenuFrameworkVersion() <= 0.0F) {
            return;
        }

        SKSEMenuFramework::SetSection("Various Book Tags");
        SKSEMenuFramework::AddSectionItem("Home", RenderSettings);
        registered = true;
    }
}
