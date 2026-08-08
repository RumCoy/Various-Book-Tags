#include "Menu.h"

#include "BookProcessor.h"
#include "Config.h"

#include <utility>
#include <Windows.h>

namespace VariousBookTags::Menu
{
    namespace
    {
        using RenderFunction = void(__stdcall*)();
        using AddSectionItemFunction = void (*)(const char*, RenderFunction);
        using CheckboxFunction = bool (*)(const char*, bool*);
        using GetVersionFunction = float (*)();
        using TextUnformattedFunction = void (*)(const char*, const char*);

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

        void Text(const char* value)
        {
            static const auto function =
                GetFrameworkFunction<TextUnformattedFunction>("igTextUnformatted");
            if (function) {
                function(value, nullptr);
            }
        }

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
            if (Checkbox("Enable Various Book Tags", &settings.enabled)) {
                update.enabled = settings.enabled;
            }
            if (Checkbox("Enable skill and spell-school tags", &settings.skillTags)) {
                update.skillTags = settings.skillTags;
            }
            if (Checkbox("Enable mod-name tags", &settings.modNameTags)) {
                update.modNameTags = settings.modNameTags;
            }
            if (Checkbox("Tag unconfigured mods with plugin names", &settings.fallback)) {
                update.globalPluginNameFallback = settings.fallback;
            }
            Text("Uses the plugin filename as the tag for books from unconfigured non-vanilla plugins.");
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

        if (!GetFrameworkModule()) {
            return;
        }

        const auto getVersion =
            GetFrameworkFunction<GetVersionFunction>("GetMenuFrameworkVersion");
        const auto addSectionItem =
            GetFrameworkFunction<AddSectionItemFunction>("AddSectionItem");
        const auto checkbox = GetFrameworkFunction<CheckboxFunction>("igCheckbox");
        const auto text = GetFrameworkFunction<TextUnformattedFunction>("igTextUnformatted");
        if (!getVersion || !addSectionItem || !checkbox || !text) {
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
