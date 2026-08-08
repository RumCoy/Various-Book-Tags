#include "Menu.h"

#include "BookProcessor.h"
#include "Config.h"

#include <Windows.h>

namespace VariousBookTags::Menu
{
    namespace
    {
        using RenderFunction = void(__stdcall*)();
        using AddSectionItemFunction = void (*)(const char*, RenderFunction);
        using CheckboxFunction = bool (*)(const char*, bool*);
        using GetVersionFunction = float (*)();

        struct MenuSettings
        {
            bool enabled{};
            bool skillTags{};
            bool modNameTags{};
            bool fallback{};
        };

        HMODULE GetFrameworkModule() noexcept
        {
            static HMODULE module = nullptr;
            if (!module) {
                module = ::GetModuleHandleW(L"SKSEMenuFramework");
            }
            return module;
        }

        template <class T>
        T GetFrameworkFunction(const char* name) noexcept
        {
            const auto module = GetFrameworkModule();
            return module ? reinterpret_cast<T>(::GetProcAddress(module, name)) : nullptr;
        }

        bool Checkbox(const char* label, bool* value)
        {
            static const auto function = GetFrameworkFunction<CheckboxFunction>("igCheckbox");
            return function && function(label, value);
        }

        void QueueSettingsUpdate(MenuSettings settings)
        {
            auto* tasks = SKSE::GetTaskInterface();
            if (!tasks) {
                SKSE::log::error("Task interface unavailable");
                return;
            }

            tasks->AddTask([settings]() {
                auto& config = Config::GetSingleton();
                config.SetEnabled(settings.enabled);
                config.SetSkillTagsEnabled(settings.skillTags);
                config.SetModNameTagsEnabled(settings.modNameTags);
                config.SetGlobalPluginNameFallbackEnabled(settings.fallback);
                if (!config.SaveTempCache()) {
                    SKSE::log::error("Settings write failed");
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

            bool changed = false;
            if (Checkbox("Enable Various Book Tags", &settings.enabled)) {
                changed = true;
            }
            if (Checkbox("Enable skill and spell-school tags", &settings.skillTags)) {
                changed = true;
            }
            if (Checkbox("Enable mod-name tags", &settings.modNameTags)) {
                changed = true;
            }
            if (Checkbox("Tag unconfigured mods with plugin names", &settings.fallback)) {
                changed = true;
            }
            if (changed) {
                QueueSettingsUpdate(settings);
            }
        }
    }

    void Register()
    {
        static bool registered = false;
        if (registered) {
            return;
        }

        if (!GetFrameworkModule()) {
            return;
        }

        const auto getVersion =
            GetFrameworkFunction<GetVersionFunction>("GetMenuFrameworkVersion");
        const auto addSectionItem =
            GetFrameworkFunction<AddSectionItemFunction>("AddSectionItem");
        const auto checkbox = GetFrameworkFunction<CheckboxFunction>("igCheckbox");
        if (!getVersion || !addSectionItem || !checkbox) {
            SKSE::log::warn("SKSEMF API unavailable");
            return;
        }

        const float version = getVersion();
        if (version <= 0.0F) {
            SKSE::log::warn("SKSEMF version invalid");
            return;
        }

        addSectionItem("Various Book Tags/General", RenderSettings);
        registered = true;
    }
}
