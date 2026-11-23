// Coverage-guided libFuzzer harness (optional; requires TEMPO_LIBFUZZER=ON).
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "tempo/limits.hpp"
#include "tempo/record.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size > tempo::limits::kMaxRecordBytes) {
    return 0;
  }
  const std::string_view bytes(reinterpret_cast<const char*>(data), size);
  (void)tempo::load_record_bytes(bytes, tempo::LoadMode::Recover);
  (void)tempo::load_record_bytes(bytes, tempo::LoadMode::Strict);
  return 0;
}
