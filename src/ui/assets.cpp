#include "tempo/assets.hpp"

#include "tempo_assets.hpp"

namespace tempo::assets {

const EmbeddedAsset* find_asset(std::string_view name) noexcept {
  for (std::size_t i = 0; i < kAssetCount; ++i) {
    if (kAssets[i].name == name) {
      return reinterpret_cast<const EmbeddedAsset*>(&kAssets[i]);
    }
  }
  return nullptr;
}

std::string_view asset_mime(std::string_view name) noexcept {
  if (name.size() >= 5 && name.substr(name.size() - 5) == ".html") {
    return "text/html; charset=utf-8";
  }
  if (name.size() >= 4 && name.substr(name.size() - 4) == ".css") {
    return "text/css; charset=utf-8";
  }
  if (name.size() >= 3 && name.substr(name.size() - 3) == ".js") {
    return "text/javascript; charset=utf-8";
  }
  if (name.size() >= 4 && name.substr(name.size() - 4) == ".svg") {
    return "image/svg+xml";
  }
  return "application/octet-stream";
}

std::string_view index_html() noexcept {
  const EmbeddedAsset* asset = find_asset("index.html");
  return asset == nullptr ? std::string_view() : asset->body;
}

}  // namespace tempo::assets
