#include "BookProcessor.h"

#include "Config.h"
#include "FormOrigin.h"

#include <SKSE/SKSE.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace
{
    bool EqualsIgnoreCase(std::string_view left, std::string_view right)
    {
        return left.size() == right.size() &&
               std::ranges::equal(left, right, [](unsigned char a, unsigned char b) {
                   return std::tolower(a) == std::tolower(b);
               });
    }

    bool IsVanillaMaster(std::string_view filename)
    {
        static constexpr std::array masters{
            "Skyrim.esm"sv,
            "Update.esm"sv,
            "Dawnguard.esm"sv,
            "HearthFires.esm"sv,
            "Dragonborn.esm"sv
        };
        return std::ranges::any_of(masters,
            [filename](std::string_view master) { return EqualsIgnoreCase(filename, master); });
    }

    std::string MakeFallbackTag(std::string_view filename)
    {
        const auto separator = filename.find_last_of('.');
        if (separator != std::string_view::npos) {
            const auto extension = filename.substr(separator);
            if (EqualsIgnoreCase(extension, ".esp") ||
                EqualsIgnoreCase(extension, ".esm") ||
                EqualsIgnoreCase(extension, ".esl")) {
                filename = filename.substr(0, separator);
            }
        }
        return std::string{ filename };
    }

    void AppendTag(std::string& title, std::string_view tag)
    {
        if (tag.empty()) {
            return;
        }
        title.push_back('(');
        title.append(tag);
        title.push_back(')');
    }

    struct ClassTag
    {
        std::string_view text;
        bool spell{};
    };

    std::string_view ClassLabel(RE::ActorValue actorValue)
    {
        switch (actorValue) {
        case RE::ActorValue::kOneHanded: return "One Handed";
        case RE::ActorValue::kTwoHanded: return "Two Handed";
        case RE::ActorValue::kArchery: return "Archery";
        case RE::ActorValue::kBlock: return "Block";
        case RE::ActorValue::kSmithing: return "Smithing";
        case RE::ActorValue::kHeavyArmor: return "Heavy Armor";
        case RE::ActorValue::kLightArmor: return "Light Armor";
        case RE::ActorValue::kPickpocket: return "Pickpocket";
        case RE::ActorValue::kLockpicking: return "Lockpicking";
        case RE::ActorValue::kSneak: return "Sneak";
        case RE::ActorValue::kAlchemy: return "Alchemy";
        case RE::ActorValue::kSpeech: return "Speech";
        case RE::ActorValue::kAlteration: return "Alteration";
        case RE::ActorValue::kConjuration: return "Conjuration";
        case RE::ActorValue::kDestruction: return "Destruction";
        case RE::ActorValue::kIllusion: return "Illusion";
        case RE::ActorValue::kRestoration: return "Restoration";
        case RE::ActorValue::kEnchanting: return "Enchanting";
        default: return {};
        }
    }

    ClassTag ResolveClassTag(RE::TESObjectBOOK* book,
        const VariousBookTags::Config& config, const VariousBookTags::Rule& rule)
    {
        if (!config.ClassTagsEnabled() || !rule.classTags) {
            return {};
        }

        if (book->TeachesSpell()) {
            if (auto* spell = book->GetSpell()) {
                return { ClassLabel(spell->GetAssociatedSkill()), true };
            }
            return {};
        }

        if (book->TeachesSkill()) {
            return { ClassLabel(book->GetSkill()), false };
        }
        return {};
    }
}

namespace VariousBookTags::BookProcessor
{
    void Apply()
    {
        auto& config = Config::GetSingleton();
        if (!config.Enabled()) {
            SKSE::log::info("Book tagging is disabled");
            return;
        }

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            SKSE::log::critical("Unable to access TESDataHandler");
            return;
        }

        std::size_t inspected = 0;
        std::size_t changed = 0;
        std::size_t skillTagged = 0;
        std::size_t spellTagged = 0;
        std::size_t sourceTagged = 0;
        std::size_t fallbackTagged = 0;

        for (auto* book : dataHandler->GetFormArray<RE::TESObjectBOOK>()) {
            if (!book) {
                continue;
            }
            ++inspected;

            const char* currentName = book->GetFullName();
            if (!currentName || currentName[0] == '\0') {
                continue;
            }

            const auto origin = FormOrigin::Resolve(book);
            if (!origin) {
                continue;
            }
            const bool vanilla = IsVanillaMaster(origin->filename);
            Rule fallbackRule;
            const auto* rule = config.FindRule(origin->filename);
            bool usingFallback = false;
            if (!rule) {
                if (!config.GlobalPluginNameFallbackEnabled() || vanilla) {
                    continue;
                }
                fallbackRule.tag = MakeFallbackTag(origin->filename);
                rule = &fallbackRule;
                usingFallback = true;
            }
            if (!rule->Allows(origin->localFormID)) {
                continue;
            }

            const auto classTag = ResolveClassTag(book, config, *rule);
            std::string_view sourceTag;
            if (config.SourceTagsEnabled() && rule->sourceTags &&
                !vanilla) {
                sourceTag = rule->tag;
            }

            if (classTag.text.empty() && sourceTag.empty()) {
                continue;
            }

            const std::string original{ currentName };
            std::string output{ original };
            if (!std::isspace(static_cast<unsigned char>(output.back()))) {
                output.push_back(' ');
            }
            AppendTag(output, classTag.text);
            AppendTag(output, sourceTag);
            book->fullName = output;
            ++changed;
            if (!classTag.text.empty()) {
                classTag.spell ? ++spellTagged : ++skillTagged;
            }
            if (!sourceTag.empty()) {
                ++sourceTagged;
                if (usingFallback) {
                    ++fallbackTagged;
                }
            }

            SKSE::log::debug("Tagged {}|{:X}: {} -> {}",
                origin->filename, origin->localFormID, original, output);
        }

        SKSE::log::info(
            "Book processing complete: inspected={}; changed={}; skill={}; spell={}; source={}; fallback={}",
            inspected, changed, skillTagged, spellTagged, sourceTagged, fallbackTagged);
    }
}
