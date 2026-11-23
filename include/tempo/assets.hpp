#pragma once

#include <string_view>

namespace tempo::assets {

struct EmbeddedAsset {
  std::string_view name;
  std::string_view body;
};

/// All UI assets are compiled into the binary. Names are base filenames.
const EmbeddedAsset* find_asset(std::string_view name) noexcept;

/// MIME type for an embedded asset, or application/octet-stream.
std::string_view asset_mime(std::string_view name) noexcept;

/// The workbench entry document (embedded index.html).
std::string_view index_html() noexcept;

}  // namespace tempo::assets
