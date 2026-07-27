#pragma once

#include <string_view>

/**
 * @file atlas.h
 * @brief Declares Atlas library-wide API information.
 */

namespace Atlas
{
    /**
     * @brief Gets the Atlas library version.
     * @return The library version string.
     */
    std::string_view version() noexcept;
} // namespace Atlas
