#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace tempo {

/// CRC32 (ISO-HDLC, polynomial 0xEDB88320). Used for record integrity.
/// The seed is the previous finalized CRC, so chunked updates compose.
std::uint32_t crc32(std::string_view bytes, std::uint32_t seed = 0) noexcept;
std::uint32_t crc32_update(std::uint32_t crc, const void* data, std::size_t size) noexcept;

}  // namespace tempo
