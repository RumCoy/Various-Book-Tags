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
#include <unordered_map>
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

    bool IsVanillaPlugin(std::string_view filename)
    {
        static constexpr std::array vanillaPlugins{
            "Skyrim.esm"sv,
            "Update.esm"sv,
            "Dawnguard.esm"sv,
            "HearthFires.esm"sv,
            "Dragonborn.esm"sv,
            "ccasvsse001-almsivi.esm"sv,
            "ccBGSSSE001-Fish.esm"sv,
            "ccbgssse003-zombies.esl"sv,
            "ccbgssse005-goldbrand.esl"sv,
            "ccbgssse020-graycowl.esl"sv,
            "cctwbsse001-puzzledungeon.esm"sv,
            "cceejsse001-hstead.esm"sv,
            "ccbgssse035-petnhound.esl"sv,
            "ccvsvsse002-pets.esl"sv,
            "ccbgssse034-mntuni.esm"sv,
            "ccbgssse036-petbwolf.esl"sv,
            "ccffbsse001-imperialdragon.esl"sv,
            "ccmtysse002-ve.esl"sv,
            "cceejsse003-hollow.esm"sv,
            "ccbgssse031-advcyrus.esm"sv,
            "ccbgssse038-bowofshadows.esl"sv,
            "ccbgssse040-advobgobs.esl"sv,
            "ccbgssse059-ba_dragonplate.esl"sv,
            "ccbgssse041-netchleather.esl"sv,
            "ccbgssse063-ba_ebony.esl"sv,
            "ccbgssse055-ba_orcishscaled.esl"sv,
            "ccbgssse051-ba_daedricmail.esl"sv,
            "ccbgssse067-daedinv.esm"sv,
            "ccbgssse068-bloodfall.esl"sv,
            "ccbgssse069-contest.esl"sv,
            "ccVSVSSE003-NecroArts.esl"sv,
            "ccvsvsse004-beafarmer.esl"sv,
            "ccbgssse025-advdsgs.esm"sv,
            "ccrmssse001-necrohouse.esl"sv,
            "ccedhsse003-redguard.esl"sv,
            "cceejsse004-hall.esl"sv,
            "cceejsse005-cave.esm"sv,
            "cckrtsse001_altar.esl"sv,
            "ccafdsse001-dwesanctuary.esm"sv
        };
        return std::ranges::any_of(vanillaPlugins,
            [filename](std::string_view plugin) { return EqualsIgnoreCase(filename, plugin); });
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

    struct SkillTag
    {
        std::string_view text;
        bool spell{};
    };

    std::string_view SkillLabel(RE::ActorValue actorValue)
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

    SkillTag ResolveSkillTag(RE::TESObjectBOOK* book,
        const VariousBookTags::Config& config, const VariousBookTags::Rule& rule)
    {
        if (!config.SkillTagsEnabled() || !rule.skillTags) {
            return {};
        }

        if (book->TeachesSpell()) {
            if (auto* spell = book->GetSpell()) {
                return { SkillLabel(spell->GetAssociatedSkill()), true };
            }
            return {};
        }

        if (book->TeachesSkill()) {
            return { SkillLabel(book->GetSkill()), false };
        }
        return {};
    }

    struct NameState
    {
        std::string baseName;
        std::string appliedName;
    };

    std::unordered_map<RE::TESObjectBOOK*, NameState> nameStates;

    std::size_t RestoreBaseNames(RE::TESDataHandler* dataHandler)
    {
        std::size_t restored = 0;
        for (auto* book : dataHandler->GetFormArray<RE::TESObjectBOOK>()) {
            if (!book) {
                continue;
            }

            const char* currentName = book->GetFullName();
            if (!currentName) {
                continue;
            }

            auto [entry, inserted] = nameStates.try_emplace(
                book, NameState{ currentName, currentName });
            auto& state = entry->second;
            if (!inserted && currentName != state.appliedName) {
                state.baseName = currentName;
            }
            if (currentName != state.baseName) {
                book->fullName = state.baseName;
                ++restored;
            }
            state.appliedName = state.baseName;
        }
        return restored;
    }

    void RememberAppliedName(RE::TESObjectBOOK* book, std::string_view name)
    {
        if (const auto found = nameStates.find(book); found != nameStates.end()) {
            found->second.appliedName = name;
        }
    }
}

namespace VariousBookTags::BookProcessor
{
    void Apply()
    {
        auto& config = Config::GetSingleton();
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            SKSE::log::critical("Unable to access TESDataHandler");
            return;
        }

        const std::size_t restored = RestoreBaseNames(dataHandler);
        if (!config.Enabled()) {
            SKSE::log::info("Book tagging disabled: restored={}", restored);
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
            const bool vanilla = IsVanillaPlugin(provenance->origin.filename);
            Rule implicitRule;
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
            if (!rule && vanilla) {
                rule = std::addressof(implicitRule);
                tagIdentity = std::addressof(provenance->origin);
            }
            bool usingFallback = false;
            if (!rule) {
                if (!config.GlobalPluginNameFallbackEnabled()) {
                    continue;
                }
                tagIdentity = std::addressof(provenance->winner);
                implicitRule.tag = MakeFallbackTag(tagIdentity->filename);
                rule = &implicitRule;
                usingFallback = true;
            }
            if (!tagIdentity || !rule->Allows(tagIdentity->localFormID)) {
                continue;
            }

            const auto skillTag = ResolveSkillTag(book, config, *rule);
            std::string_view modNameTag;
            if (config.ModNameTagsEnabled() && rule->modNameTags &&
                !vanilla) {
                modNameTag = rule->tag;
            }

            if (skillTag.text.empty() && modNameTag.empty()) {
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
            AppendTag(output, skillTag.text);
            AppendTag(output, modNameTag);
            book->fullName = output;
            RememberAppliedName(book, output);
            ++changed;
            if (!skillTag.text.empty()) {
                skillTag.spell ? ++spellTagged : ++skillTagged;
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
            "Book processing complete: inspected={}; restored={}; changed={}; skill={}; spell={}; modName={}; fallback={}",
            inspected, restored, changed, skillTagged, spellTagged,
            modNameTagged, fallbackTagged);
    }
}
