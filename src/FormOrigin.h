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

    [[nodiscard]] std::optional<Identity> Resolve(const RE::TESForm* form);
}
