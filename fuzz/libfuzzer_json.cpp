// Coverage-guided libFuzzer harness (optional; requires TEMPO_LIBFUZZER=ON).
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "tempo/json.hpp"
#include "tempo/limits.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size > tempo::limits::kMaxJsonBytes) {
    return 0;
  }
  const std::string_view text(reinterpret_cast<const char*>(data), size);
  const auto parsed = tempo::json::parse(text);
  if (parsed.ok) {
    const std::string dumped = parsed.value.dump();
    (void)tempo::json::parse(dumped);
  }
  return 0;
}
