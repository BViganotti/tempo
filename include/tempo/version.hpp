#pragma once

#include <string_view>

namespace tempo {

/// Semantic version of the Tempo library and launcher.
inline constexpr std::string_view kVersion = "0.2.0";

/// On-disk record format version understood by this build.
inline constexpr int kRecordFormatVersion = 1;

/// Human-readable product name used by the UI and report tooling.
inline constexpr std::string_view kProductName = "Tempo";

/// Returns a short version banner, e.g. "Tempo 0.1.0 (record v1)".
std::string_view version_banner() noexcept;

}  // namespace tempo
