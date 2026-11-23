#pragma once

#include <cstddef>
#include <cstdint>

namespace tempo::limits {

// Record files ---------------------------------------------------------------
inline constexpr std::size_t kMaxRecordBytes = 64u * 1024u * 1024u;
inline constexpr std::uint32_t kMaxEvents = 1'000'000;
inline constexpr std::uint32_t kMaxStringCount = 200'000;
inline constexpr std::size_t kMaxStringBytes = 65'536;
inline constexpr std::size_t kMaxStringTableBytes = 16u * 1024u * 1024u;
inline constexpr std::size_t kMaxMetadataBytes = 64u * 1024u;

// Event payloads -------------------------------------------------------------
inline constexpr std::size_t kMaxEventString = 8u * 1024u;
inline constexpr std::size_t kMaxEventPayload = 64u * 1024u;

// Runtime --------------------------------------------------------------------
inline constexpr std::uint32_t kMaxTasks = 1'024;
inline constexpr std::uint32_t kMaxSteps = 2'000'000;
inline constexpr std::uint64_t kMaxVirtualNs = 30ull * 24ull * 3600ull * 1'000'000'000ull;
inline constexpr std::uint32_t kMaxWatchVarsPerTask = 64;
inline constexpr std::size_t kMaxWatchName = 64;
inline constexpr std::size_t kMaxWatchValue = 512;
inline constexpr std::uint32_t kMaxJoiners = 1'024;

// Capabilities ---------------------------------------------------------------
inline constexpr std::size_t kMaxPathBytes = 1'024;
inline constexpr std::size_t kMaxFileBytes = 64u * 1024u;
inline constexpr std::size_t kMaxEnvBytes = 4u * 1024u;
inline constexpr std::size_t kMaxSocketPayload = 16u * 1024u;
inline constexpr std::size_t kMaxSocketConnections = 32;
inline constexpr std::size_t kMaxLogBytes = 4u * 1024u;
inline constexpr std::uint16_t kMaxPort = 65'535;

// UI / parsers ---------------------------------------------------------------
inline constexpr std::uint32_t kMaxJsonDepth = 32;
inline constexpr std::size_t kMaxJsonBytes = 1u * 1024u * 1024u;
inline constexpr std::size_t kMaxHttpHeaderBytes = 16u * 1024u;
inline constexpr std::size_t kMaxHttpBodyBytes = 256u * 1024u;
inline constexpr std::size_t kMaxHttpResponseBytes = 4u * 1024u * 1024u;
inline constexpr std::size_t kMaxHttpConnections = 32;
inline constexpr std::size_t kMaxHttpOutBytes = 8u * 1024u * 1024u;
inline constexpr std::size_t kMaxQueryBytes = 2u * 1024u;
inline constexpr std::size_t kMaxBreakpoints = 128;
inline constexpr std::size_t kMaxStoredRecords = 16;
inline constexpr std::uint32_t kMaxReplayRuns = 200;
inline constexpr std::uint32_t kCheckpointInterval = 32;
inline constexpr std::uint32_t kMaxEventPage = 500;

}  // namespace tempo::limits
