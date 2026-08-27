#pragma once

#include <RE/Skyrim.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace VariousBookTags
{
    struct MenuSettingsUpdate
    {
        std::optional<bool> enabled;
        std::optional<bool> skillTags;
        std::optional<bool> modNameTags;
        std::optional<bool> globalPluginNameFallback;
    };

    struct Rule
    {
        std::string tag;
        std::unordered_set<std::uint32_t> includeForms;
        std::unordered_set<std::uint32_t> excludeForms;
        bool skillTags{ true };
        bool modNameTags{ true };

        [[nodiscard]] bool Allows(std::uint32_t localFormID) const;
    };

    class Config
    {
    public:
        static Config& GetSingleton();

        bool Load(std::string_view embeddedInternalData,
            const std::filesystem::path& userConfigPath,
            const std::filesystem::path& tempCachePath);

        [[nodiscard]] bool Enabled() const noexcept;
        [[nodiscard]] bool SkillTagsEnabled() const noexcept;
        [[nodiscard]] bool ModNameTagsEnabled() const noexcept;
        [[nodiscard]] bool GlobalPluginNameFallbackEnabled() const noexcept;
        [[nodiscard]] bool ApplyMenuSettings(const MenuSettingsUpdate& update);
        [[nodiscard]] const Rule* FindRule(std::string_view pluginName) const;

    private:
        void Reset();
        bool LoadUserFile(const std::filesystem::path& path);
        bool LoadTempCache(const std::filesystem::path& path);
        std::size_t LoadDynamicFiles(const std::filesystem::path& directory);
        bool LoadStream(std::istream& input, std::string sourceName,
            bool loadGeneralSettings, bool loadPluginRules, bool userOverride);
        [[nodiscard]] bool PersistMenuSetting(
            std::string_view key, std::string_view value) const;
        [[nodiscard]] bool SyncUserGeneralSettingsToTempCache() const;
        [[nodiscard]] bool UpdateGeneralSetting(const std::filesystem::path& path,
            std::string_view key, std::string_view value, bool createIfMissing) const;

        bool enabled_{ true };
        bool skillTagsEnabled_{ true };
        bool modNameTagsEnabled_{ true };
        bool globalPluginNameFallbackEnabled_{ false };
        std::optional<bool> userEnabledOverride_;
        std::optional<bool> userSkillTagsOverride_;
        std::optional<bool> userModNameTagsOverride_;
        std::optional<bool> userGlobalPluginNameFallbackOverride_;
        std::filesystem::path userConfigPath_;
        std::filesystem::path tempCachePath_;
        std::unordered_map<std::string, Rule> rules_;
    };
}
