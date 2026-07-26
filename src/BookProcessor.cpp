#include "BookProcessor.h"

#include "Config.h"
#include "FormOrigin.h"

#include <SKSE/SKSE.h>
#include <SKSE/Translation.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

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
        std::size_t modNameTagged = 0;
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

            const auto provenance = FormOrigin::Resolve(book);
            if (!provenance) {
                continue;
            }
            const bool vanilla = IsVanillaMaster(provenance->origin.filename);
            Rule fallbackRule;
            const FormOrigin::Identity* tagIdentity = nullptr;
            const Rule* rule = nullptr;
            if (provenance->winner.file != provenance->origin.file) {
                rule = config.FindRule(provenance->winner.filename);
                if (rule) {
                    tagIdentity = std::addressof(provenance->winner);
                }
            }
            if (!rule) {
                rule = config.FindRule(provenance->origin.filename);
                if (rule) {
                    tagIdentity = std::addressof(provenance->origin);
                }
            }
            bool usingFallback = false;
            if (!rule) {
                if (!config.GlobalPluginNameFallbackEnabled() || vanilla) {
                    continue;
                }
                tagIdentity = std::addressof(provenance->winner);
                fallbackRule.tag = MakeFallbackTag(tagIdentity->filename);
                rule = &fallbackRule;
                usingFallback = true;
            }
            if (!tagIdentity || !rule->Allows(tagIdentity->localFormID)) {
                continue;
            }

            const auto classTag = ResolveClassTag(book, config, *rule);
            std::string_view modNameTag;
            if (config.ModNameTagsEnabled() && rule->modNameTags &&
                !vanilla) {
                modNameTag = rule->tag;
            }

            if (classTag.text.empty() && modNameTag.empty()) {
                continue;
            }

            std::string original{ currentName };
            if (original.starts_with('$')) {
                std::string translated;
                if (!SKSE::Translation::Translate(original, translated) || translated.empty()) {
                    SKSE::log::debug("Deferred unresolved book localization token: {}", original);
                    continue;
                }
                original = std::move(translated);
            }
            std::string output{ original };
            if (!std::isspace(static_cast<unsigned char>(output.back()))) {
                output.push_back(' ');
            }
            AppendTag(output, classTag.text);
            AppendTag(output, modNameTag);
            book->fullName = output;
            ++changed;
            if (!classTag.text.empty()) {
                classTag.spell ? ++spellTagged : ++skillTagged;
            }
            if (!modNameTag.empty()) {
                ++modNameTagged;
                if (usingFallback) {
                    ++fallbackTagged;
                }
            }

            SKSE::log::debug("Tagged {}|{:X}: {} -> {}",
                tagIdentity->filename, tagIdentity->localFormID, original, output);
        }

        SKSE::log::info(
            "Book processing complete: inspected={}; changed={}; skill={}; spell={}; modName={}; fallback={}",
            inspected, changed, skillTagged, spellTagged, modNameTagged, fallbackTagged);
    }
}
