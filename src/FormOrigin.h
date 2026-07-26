#pragma once

#include <RE/Skyrim.h>

#include <cstdint>
#include <optional>
#include <string>

namespace VariousBookTags::FormOrigin
{
    struct Identity
    {
        const RE::TESFile* file{};
        std::string filename;
        std::uint32_t localFormID{};
    };

    struct Provenance
    {
        Identity origin;
        Identity winner;
    };

    [[nodiscard]] std::optional<Provenance> Resolve(const RE::TESForm* form);
}
