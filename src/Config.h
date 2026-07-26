#pragma once

#include <RE/Skyrim.h>

#include <cstdint>
#include <filesystem>
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
        bool classTags{ true };
        bool sourceTags{ true };

        [[nodiscard]] bool Allows(std::uint32_t localFormID) const;
    };

    class Config
    {
    public:
        static Config& GetSingleton();

        bool Load(const std::filesystem::path& mainPath,
            const std::filesystem::path& userPath);

        [[nodiscard]] bool Enabled() const noexcept;
        [[nodiscard]] bool ClassTagsEnabled() const noexcept;
        [[nodiscard]] bool SourceTagsEnabled() const noexcept;
        [[nodiscard]] bool GlobalPluginNameFallbackEnabled() const noexcept;
        [[nodiscard]] const Rule* FindRule(std::string_view pluginName) const;

    private:
        void Reset();
        bool LoadFile(const std::filesystem::path& path, bool optional);

        bool enabled_{ true };
        bool classTagsEnabled_{ true };
        bool sourceTagsEnabled_{ true };
        bool globalPluginNameFallbackEnabled_{ false };
        std::unordered_map<std::string, Rule> rules_;
    };
}
