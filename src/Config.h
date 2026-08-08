#pragma once

#include <RE/Skyrim.h>

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace VariousBookTags
{
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
        void SetEnabled(bool enabled) noexcept;
        [[nodiscard]] bool SkillTagsEnabled() const noexcept;
        void SetSkillTagsEnabled(bool enabled) noexcept;
        [[nodiscard]] bool ModNameTagsEnabled() const noexcept;
        void SetModNameTagsEnabled(bool enabled) noexcept;
        [[nodiscard]] bool GlobalPluginNameFallbackEnabled() const noexcept;
        void SetGlobalPluginNameFallbackEnabled(bool enabled) noexcept;
        [[nodiscard]] bool SaveTempCache() const;
        [[nodiscard]] const Rule* FindRule(std::string_view pluginName) const;

    private:
        void Reset();
        bool LoadUserFile(const std::filesystem::path& path);
        bool LoadTempCache(const std::filesystem::path& path);
        bool LoadStream(std::istream& input, std::string sourceName,
            bool loadPluginRules = true);

        bool enabled_{ true };
        bool skillTagsEnabled_{ true };
        bool modNameTagsEnabled_{ true };
        bool globalPluginNameFallbackEnabled_{ false };
        std::filesystem::path tempCachePath_;
        std::unordered_map<std::string, Rule> rules_;
    };
}
