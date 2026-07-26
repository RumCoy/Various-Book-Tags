#include "Config.h"

#include <SKSE/SKSE.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>

namespace
{
    std::string Trim(std::string value)
    {
        const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
        value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
        return value;
    }

    std::string Lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return value;
    }

    std::string CanonicalKey(std::string value)
    {
        value = Lower(Trim(std::move(value)));
        std::erase_if(value, [](unsigned char ch) {
            return std::isspace(ch) || ch == '-' || ch == '_';
        });
        return value;
    }

    bool ParseBool(std::string value, bool fallback)
    {
        value = Lower(Trim(std::move(value)));
        if (value == "true" || value == "1" || value == "yes" || value == "on") {
            return true;
        }
        if (value == "false" || value == "0" || value == "no" || value == "off") {
            return false;
        }
        return fallback;
    }

    std::optional<std::uint32_t> ParseFormID(std::string token)
    {
        token = Trim(std::move(token));
        if (token.empty()) {
            return std::nullopt;
        }
        if (token.starts_with("0x") || token.starts_with("0X")) {
            token.erase(0, 2);
        }

        std::uint32_t value{};
        const auto [ptr, error] = std::from_chars(
            token.data(), token.data() + token.size(), value, 16);
        if (error != std::errc{} || ptr != token.data() + token.size()) {
            return std::nullopt;
        }
        return value;
    }

    void ParseFormList(std::string value, std::unordered_set<std::uint32_t>& destination)
    {
        std::stringstream stream(std::move(value));
        std::string token;
        while (std::getline(stream, token, ',')) {
            if (const auto parsed = ParseFormID(std::move(token))) {
                destination.insert(*parsed);
            }
        }
    }

}

namespace VariousBookTags
{
    bool Rule::Allows(std::uint32_t localFormID) const
    {
        if (excludeForms.contains(localFormID)) {
            return false;
        }
        return includeForms.empty() || includeForms.contains(localFormID);
    }

    Config& Config::GetSingleton()
    {
        static Config instance;
        return instance;
    }

    void Config::Reset()
    {
        enabled_ = true;
        classTagsEnabled_ = true;
        modNameTagsEnabled_ = true;
        globalPluginNameFallbackEnabled_ = false;
        rules_.clear();
    }

    bool Config::Load(std::string_view embeddedInternalData,
        const std::filesystem::path& userConfigPath)
    {
        Reset();

        std::istringstream internalDataInput{ std::string(embeddedInternalData) };
        const bool loadedEmbeddedData = embeddedInternalData.empty() ?
            false : LoadStream(internalDataInput, "embedded internal data");
        const bool loadedUserConfig = LoadUserFile(userConfigPath);

        SKSE::log::info(
            "Configuration complete: {} book rule(s); enabled={}; class={}; modNameTags={}; globalPluginNameFallback={}; embeddedData={}; userConfig={}",
            rules_.size(), enabled_, classTagsEnabled_,
            modNameTagsEnabled_, globalPluginNameFallbackEnabled_,
            loadedEmbeddedData, loadedUserConfig);
        return loadedEmbeddedData || loadedUserConfig;
    }

    bool Config::LoadUserFile(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        if (!input) {
            SKSE::log::info("Optional user configuration not found: {}", path.string());
            return false;
        }

        return LoadStream(input, path.filename().string());
    }

    bool Config::LoadStream(std::istream& input, std::string sourceName)
    {
        std::unordered_map<std::string, Rule> fileRules;
        enum class Section { kOther, kGeneral, kPlugin };
        Section activeSection = Section::kOther;
        Rule* activeRule = nullptr;
        std::string line;
        std::size_t lineNumber = 0;
        while (std::getline(input, line)) {
            ++lineNumber;
            line = Trim(std::move(line));
            if (line.empty() || line.starts_with(';') || line.starts_with('#')) {
                continue;
            }

            if (line.front() == '[' && line.back() == ']') {
                auto section = Trim(line.substr(1, line.size() - 2));
                const auto lowered = Lower(section);
                activeSection = Section::kOther;
                activeRule = nullptr;
                constexpr std::string_view prefix = "plugin:";
                if (lowered == "general") {
                    activeSection = Section::kGeneral;
                } else if (lowered.starts_with(prefix)) {
                    auto pluginName = Trim(section.substr(prefix.size()));
                    if (!pluginName.empty()) {
                        auto result = fileRules.insert_or_assign(Lower(pluginName), Rule{});
                        activeRule = std::addressof(result.first->second);
                        activeSection = Section::kPlugin;
                    }
                }
                continue;
            }

            const auto separator = line.find('=');
            if (separator == std::string::npos) {
                SKSE::log::warn("Ignored malformed line {} in {}",
                    lineNumber, sourceName);
                continue;
            }

            auto key = Trim(line.substr(0, separator));
            auto value = Trim(line.substr(separator + 1));
            if (activeSection == Section::kGeneral) {
                const auto canonical = CanonicalKey(key);
                if (canonical == "enabled") {
                    enabled_ = ParseBool(value, enabled_);
                } else if (canonical == "classtags") {
                    classTagsEnabled_ = ParseBool(value, true);
                } else if (canonical == "modnametags") {
                    modNameTagsEnabled_ = ParseBool(value, true);
                } else if (canonical == "globalpluginnamefallback") {
                    globalPluginNameFallbackEnabled_ = ParseBool(
                        value, globalPluginNameFallbackEnabled_);
                }
                continue;
            }

            if (activeSection != Section::kPlugin || !activeRule) {
                continue;
            }

            const auto canonical = CanonicalKey(std::move(key));
            if (canonical == "tag") {
                activeRule->tag = std::move(value);
            } else if (canonical == "includeforms") {
                ParseFormList(std::move(value), activeRule->includeForms);
            } else if (canonical == "excludeforms") {
                ParseFormList(std::move(value), activeRule->excludeForms);
            } else if (canonical == "classtags") {
                activeRule->classTags = ParseBool(value, true);
            } else if (canonical == "modnametags") {
                activeRule->modNameTags = ParseBool(value, true);
            }
        }

        std::size_t loaded = 0;
        std::size_t overridden = 0;
        for (auto& [pluginName, rule] : fileRules) {
            if (rules_.contains(pluginName)) {
                ++overridden;
            }
            rules_.insert_or_assign(std::move(pluginName), std::move(rule));
            ++loaded;
        }

        SKSE::log::info("Loaded {} book rule(s) from {}; {} override(s)",
            loaded, sourceName, overridden);
        return true;
    }

    bool Config::Enabled() const noexcept
    {
        return enabled_;
    }

    bool Config::ClassTagsEnabled() const noexcept
    {
        return classTagsEnabled_;
    }

    bool Config::ModNameTagsEnabled() const noexcept
    {
        return modNameTagsEnabled_;
    }

    bool Config::GlobalPluginNameFallbackEnabled() const noexcept
    {
        return globalPluginNameFallbackEnabled_;
    }

    const Rule* Config::FindRule(std::string_view pluginName) const
    {
        const auto found = rules_.find(Lower(std::string(pluginName)));
        return found == rules_.end() ? nullptr : std::addressof(found->second);
    }
}
