#include "Config.h"

#include <SKSE/SKSE.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

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

    std::optional<bool> ParseBoolValue(std::string value)
    {
        value = Lower(Trim(std::move(value)));
        if (value == "true" || value == "1" || value == "yes" || value == "on") {
            return true;
        }
        if (value == "false" || value == "0" || value == "no" || value == "off") {
            return false;
        }
        return std::nullopt;
    }

    bool ParseBool(std::string value, bool fallback)
    {
        if (const auto parsed = ParseBoolValue(std::move(value))) {
            return *parsed;
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

    bool ParseFormList(std::string value, std::unordered_set<std::uint32_t>& destination)
    {
        bool valid = true;
        std::stringstream stream(std::move(value));
        std::string token;
        while (std::getline(stream, token, ',')) {
            if (const auto parsed = ParseFormID(std::move(token))) {
                destination.insert(*parsed);
            } else {
                valid = false;
            }
        }
        return valid;
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
        skillTagsEnabled_ = true;
        modNameTagsEnabled_ = true;
        globalPluginNameFallbackEnabled_ = false;
        rules_.clear();
    }

    bool Config::Load(std::string_view embeddedInternalData,
        const std::filesystem::path& userConfigPath,
        const std::filesystem::path& tempCachePath)
    {
        Reset();
        userConfigPath_ = userConfigPath;
        tempCachePath_ = tempCachePath;

        std::istringstream internalDataInput{ std::string(embeddedInternalData) };
        const bool loadedEmbeddedData = embeddedInternalData.empty() ?
            false : LoadStream(internalDataInput, "embedded internal data");
        const bool loadedTempCache = LoadTempCache(tempCachePath_);
        const bool loadedUserConfig = LoadUserFile(userConfigPath_);

        SKSE::log::info(
            "Configuration complete: {} book rule(s); enabled={}; skillTags={}; modNameTags={}; globalPluginNameFallback={}; embeddedData={}; userConfig={}; tempCache={}",
            rules_.size(), enabled_, skillTagsEnabled_,
            modNameTagsEnabled_, globalPluginNameFallbackEnabled_,
            loadedEmbeddedData, loadedUserConfig, loadedTempCache);
        return loadedEmbeddedData || loadedUserConfig || loadedTempCache;
    }

    bool Config::LoadUserFile(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        if (!input) {
            SKSE::log::info("Optional user configuration not found: {}", path.string());
            return false;
        }

        return LoadStream(input, path.filename().string(), true, true);
    }

    bool Config::LoadTempCache(const std::filesystem::path& path)
    {
        if (path.empty()) {
            return false;
        }

        std::ifstream input(path);
        if (!input) {
            return false;
        }

        return LoadStream(input, path.filename().string(), false);
    }

    bool Config::LoadStream(std::istream& input, std::string sourceName,
        bool loadPluginRules, bool userOverride)
    {
        struct ParsedRule
        {
            Rule rule;
            bool tagSpecified{};
            bool includeFormsSpecified{};
            bool excludeFormsSpecified{};
            bool skillTagsSpecified{};
            bool modNameTagsSpecified{};
        };

        std::unordered_map<std::string, ParsedRule> fileRules;
        enum class Section { kOther, kGeneral, kPlugin };
        Section activeSection = Section::kOther;
        ParsedRule* activeRule = nullptr;
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
                } else if (loadPluginRules && lowered.starts_with(prefix)) {
                    auto pluginName = Trim(section.substr(prefix.size()));
                    if (!pluginName.empty()) {
                        const auto key = Lower(pluginName);
                        auto result = userOverride ?
                            fileRules.try_emplace(key, ParsedRule{}) :
                            fileRules.insert_or_assign(key, ParsedRule{});
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
                } else if (canonical == "skilltags") {
                    skillTagsEnabled_ = ParseBool(value, true);
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
                activeRule->tagSpecified = true;
                activeRule->rule.tag = std::move(value);
            } else if (canonical == "includeforms") {
                if (userOverride) {
                    std::unordered_set<std::uint32_t> forms;
                    if (ParseFormList(value, forms)) {
                        activeRule->includeFormsSpecified = true;
                        activeRule->rule.includeForms = std::move(forms);
                    } else {
                        SKSE::log::warn("Ignored invalid IncludeForms at line {} in {}",
                            lineNumber, sourceName);
                    }
                } else {
                    activeRule->includeFormsSpecified = true;
                    activeRule->rule.includeForms.clear();
                    ParseFormList(std::move(value), activeRule->rule.includeForms);
                }
            } else if (canonical == "excludeforms") {
                if (userOverride) {
                    std::unordered_set<std::uint32_t> forms;
                    if (ParseFormList(value, forms)) {
                        activeRule->excludeFormsSpecified = true;
                        activeRule->rule.excludeForms = std::move(forms);
                    } else {
                        SKSE::log::warn("Ignored invalid ExcludeForms at line {} in {}",
                            lineNumber, sourceName);
                    }
                } else {
                    activeRule->excludeFormsSpecified = true;
                    activeRule->rule.excludeForms.clear();
                    ParseFormList(std::move(value), activeRule->rule.excludeForms);
                }
            } else if (canonical == "skilltags") {
                if (!userOverride || value.empty()) {
                    activeRule->skillTagsSpecified = true;
                    activeRule->rule.skillTags = ParseBool(value, true);
                } else if (const auto parsed = ParseBoolValue(value)) {
                    activeRule->skillTagsSpecified = true;
                    activeRule->rule.skillTags = *parsed;
                } else {
                    SKSE::log::warn("Ignored invalid SkillTags at line {} in {}",
                        lineNumber, sourceName);
                }
            } else if (canonical == "modnametags") {
                if (!userOverride || value.empty()) {
                    activeRule->modNameTagsSpecified = true;
                    activeRule->rule.modNameTags = ParseBool(value, true);
                } else if (const auto parsed = ParseBoolValue(value)) {
                    activeRule->modNameTagsSpecified = true;
                    activeRule->rule.modNameTags = *parsed;
                } else {
                    SKSE::log::warn("Ignored invalid ModNameTags at line {} in {}",
                        lineNumber, sourceName);
                }
            }
        }

        std::size_t loaded = 0;
        std::size_t overridden = 0;
        for (auto& [pluginName, parsed] : fileRules) {
            const auto existing = rules_.find(pluginName);
            const bool hasExistingRule = existing != rules_.end();
            Rule rule;

            if (userOverride) {
                if (hasExistingRule) {
                    rule = existing->second;
                }
                if (parsed.tagSpecified) {
                    rule.tag = std::move(parsed.rule.tag);
                }
                if (parsed.includeFormsSpecified) {
                    rule.includeForms = std::move(parsed.rule.includeForms);
                }
                if (parsed.excludeFormsSpecified) {
                    rule.excludeForms = std::move(parsed.rule.excludeForms);
                }
                if (parsed.skillTagsSpecified) {
                    rule.skillTags = parsed.rule.skillTags;
                }
                if (parsed.modNameTagsSpecified) {
                    rule.modNameTags = parsed.rule.modNameTags;
                }
            } else {
                rule = std::move(parsed.rule);
            }

            if (hasExistingRule) {
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

    bool Config::SkillTagsEnabled() const noexcept
    {
        return skillTagsEnabled_;
    }

    bool Config::ModNameTagsEnabled() const noexcept
    {
        return modNameTagsEnabled_;
    }

    bool Config::GlobalPluginNameFallbackEnabled() const noexcept
    {
        return globalPluginNameFallbackEnabled_;
    }

    bool Config::ApplyMenuSettings(const MenuSettingsUpdate& update)
    {
        if (update.enabled.has_value()) {
            enabled_ = *update.enabled;
        }
        if (update.skillTags.has_value()) {
            skillTagsEnabled_ = *update.skillTags;
        }
        if (update.modNameTags.has_value()) {
            modNameTagsEnabled_ = *update.modNameTags;
        }
        if (update.globalPluginNameFallback.has_value()) {
            globalPluginNameFallbackEnabled_ = *update.globalPluginNameFallback;
        }

        bool saved = true;
        if (!SaveTempCache()) {
            SKSE::log::error("Failed to save SKSE Menu Framework settings to temp cache: {}",
                tempCachePath_.string());
            saved = false;
        }

        std::error_code error;
        const bool userConfigExists = !userConfigPath_.empty() &&
            std::filesystem::exists(userConfigPath_, error);
        if (error) {
            SKSE::log::error("Failed to check user configuration path {}: {}",
                userConfigPath_.string(), error.message());
            return false;
        }

        if (!userConfigExists) {
            return saved;
        }

        const auto persist = [this, &saved](
                                 std::string_view key, const std::optional<bool>& setting) {
            if (!setting.has_value()) {
                return;
            }
            if (!UpdateUserConfigSetting(key, *setting ? "true" : "false")) {
                SKSE::log::error(
                    "Failed to save SKSE Menu Framework setting {} to user configuration: {}",
                    key, userConfigPath_.string());
                saved = false;
            }
        };

        persist("Enabled", update.enabled);
        persist("SkillTags", update.skillTags);
        persist("ModNameTags", update.modNameTags);
        persist("GlobalPluginNameFallback", update.globalPluginNameFallback);
        return saved;
    }

    bool Config::SaveTempCache() const
    {
        if (tempCachePath_.empty()) {
            return false;
        }

        std::ofstream output(tempCachePath_, std::ios::trunc);
        if (!output) {
            return false;
        }

        output
            << "[General]\n"
            << "Enabled = " << (Enabled() ? "true" : "false") << '\n'
            << "SkillTags = " << (SkillTagsEnabled() ? "true" : "false") << '\n'
            << "ModNameTags = " << (ModNameTagsEnabled() ? "true" : "false") << '\n'
            << "GlobalPluginNameFallback = "
            << (GlobalPluginNameFallbackEnabled() ? "true" : "false") << '\n';

        return static_cast<bool>(output);
    }

    bool Config::UpdateUserConfigSetting(std::string_view key, std::string_view value) const
    {
        if (userConfigPath_.empty()) {
            return false;
        }

        std::ifstream input(userConfigPath_, std::ios::binary);
        if (!input) {
            return false;
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();
        if (input.bad()) {
            return false;
        }
        const std::string contents = buffer.str();

        struct TextLine
        {
            std::string text;
            std::string ending;
        };

        std::vector<TextLine> lines;
        std::size_t cursor = 0;
        while (cursor < contents.size()) {
            const auto newlinePosition = contents.find('\n', cursor);
            if (newlinePosition == std::string::npos) {
                lines.push_back({ contents.substr(cursor), {} });
                break;
            }

            auto textEnd = newlinePosition;
            std::string ending = "\n";
            if (textEnd > cursor && contents[textEnd - 1] == '\r') {
                --textEnd;
                ending = "\r\n";
            }
            lines.push_back({ contents.substr(cursor, textEnd - cursor), std::move(ending) });
            cursor = newlinePosition + 1;
        }

        std::string preferredLineEnding = "\n";
        for (const auto& line : lines) {
            if (!line.ending.empty()) {
                preferredLineEnding = line.ending;
                break;
            }
        }
        const bool hadFinalNewline = !contents.empty() && contents.back() == '\n';

        const auto normalizedKey = CanonicalKey(std::string(key));
        bool inGeneral = false;
        bool foundGeneral = false;
        bool trackingLastGeneral = false;
        bool updated = false;
        std::size_t lastGeneralEnd = lines.size();

        for (std::size_t i = 0; i < lines.size(); ++i) {
            auto parsedLine = Trim(lines[i].text);
            if (i == 0 && parsedLine.starts_with("\xEF\xBB\xBF")) {
                parsedLine.erase(0, 3);
            }

            if (!parsedLine.empty() && parsedLine.front() == '[' && parsedLine.back() == ']') {
                if (trackingLastGeneral) {
                    lastGeneralEnd = i;
                    trackingLastGeneral = false;
                }

                const auto sectionName = Lower(Trim(
                    parsedLine.substr(1, parsedLine.size() - 2)));
                inGeneral = sectionName == "general";
                if (inGeneral) {
                    foundGeneral = true;
                    trackingLastGeneral = true;
                    lastGeneralEnd = lines.size();
                }
                continue;
            }

            if (!inGeneral || parsedLine.empty() ||
                parsedLine.starts_with(';') || parsedLine.starts_with('#')) {
                continue;
            }

            const auto separator = lines[i].text.find('=');
            if (separator == std::string::npos ||
                CanonicalKey(lines[i].text.substr(0, separator)) != normalizedKey) {
                continue;
            }

            std::size_t valueStart = separator + 1;
            while (valueStart < lines[i].text.size() &&
                   std::isspace(static_cast<unsigned char>(lines[i].text[valueStart]))) {
                ++valueStart;
            }

            std::size_t valueEnd = lines[i].text.size();
            for (std::size_t j = valueStart; j < lines[i].text.size(); ++j) {
                const char ch = lines[i].text[j];
                if (ch == ';' || ch == '#') {
                    valueEnd = j;
                    break;
                }
            }
            while (valueEnd > valueStart &&
                   std::isspace(static_cast<unsigned char>(lines[i].text[valueEnd - 1]))) {
                --valueEnd;
            }

            lines[i].text.replace(valueStart, valueEnd - valueStart, value);
            updated = true;
        }

        if (trackingLastGeneral) {
            lastGeneralEnd = lines.size();
        }

        if (!updated) {
            TextLine settingLine{
                std::string(key) + " = " + std::string(value),
                preferredLineEnding
            };

            if (foundGeneral) {
                if (lastGeneralEnd == lines.size()) {
                    if (!lines.empty() && lines.back().ending.empty()) {
                        lines.back().ending = preferredLineEnding;
                    }
                    settingLine.ending = hadFinalNewline ? preferredLineEnding : std::string{};
                    lines.push_back(std::move(settingLine));
                } else {
                    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(lastGeneralEnd),
                        std::move(settingLine));
                }
            } else {
                if (!lines.empty()) {
                    if (lines.back().ending.empty()) {
                        lines.back().ending = preferredLineEnding;
                    }
                    if (!Trim(lines.back().text).empty()) {
                        lines.push_back({ {}, preferredLineEnding });
                    }
                }

                lines.push_back({ "[General]", preferredLineEnding });
                lines.push_back({
                    std::string(key) + " = " + std::string(value),
                    (hadFinalNewline || contents.empty()) ? preferredLineEnding : std::string{}
                });
            }
        }

        input.close();
        std::ofstream output(userConfigPath_, std::ios::binary | std::ios::trunc);
        if (!output) {
            return false;
        }
        for (const auto& line : lines) {
            output << line.text << line.ending;
        }
        return static_cast<bool>(output);
    }

    const Rule* Config::FindRule(std::string_view pluginName) const
    {
        const auto found = rules_.find(Lower(std::string(pluginName)));
        return found == rules_.end() ? nullptr : std::addressof(found->second);
    }
}
