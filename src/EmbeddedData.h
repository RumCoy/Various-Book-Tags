#pragma once

#include <string_view>

namespace VariousBookTags::EmbeddedData
{
    [[nodiscard]] std::string_view GetInternalData() noexcept;
}
