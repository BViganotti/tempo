# Record format (v1)

Tempo records are little-endian, self-describing binary files. All integers are
unsigned unless stated. Reads are bounded by the limits in
`include/tempo/limits.hpp`; a malformed file fails cleanly.

## Header (40 bytes)

| offset | size | field |
|---:|---:|---|
| 0 | 4 | magic `TMPO` (0x4F504D54) |
| 4 | 2 | format version (1) |
| 6 | 2 | flags (bit 0: complete) |
| 8 | 4 | CRC32 of bytes 0..7 |
| 12 | 8 | body offset (40) |
| 20 | 8 | body size |
| 28 | 4 | body CRC32 |
| 32 | 4 | reserved (0) |
| 36 | 4 | header CRC32 of bytes 0..35 |

## Body

1. `meta_len:u32`, `meta bytes` — `key=value\n` UTF-8 lines, bounded.
2. `string_count:u32`, then for each string `len:u32`, `bytes` (UTF-8).
3. `event_count:u32`, then event records.
4. `index_count:u32`, then `offset:u32` relative to the first event. One entry
   per 64 events (event 0, 64, 128, ...).
5. `event_bytes:u64` — total size of the event region, for validation.

## Event record

| size | field |
|---:|---|
| 1 | event type |
| 1 | flags |
| 2 | task id |
| 8 | a |
| 8 | b |
| 8 | c |
| 4 | string ref 1 |
| 4 | string ref 2 |
| 4 | payload length |
| N | payload bytes |
| 4 | CRC32 of the event record (excluding this field) |

Event types are stable numeric IDs defined in `include/tempo/event.hpp`. An
unknown type is not fatal: the reader preserves it as `Unknown` with the raw
fields so a newer file can still be inspected, but replay refuses to compare it.

## Footer (20 bytes)

| size | field |
|---:|---|
| 4 | magic `ENDT` |
| 8 | event count |
| 4 | body CRC32 |
| 4 | CRC32 of the previous 16 bytes |

## Recovery

`load_record(..., LoadMode::Recover)` accepts a truncated file: it parses
complete events, stops at the first incomplete or CRC-invalid event, and marks
the record `recovered=true`, `complete=false`. A complete file with a bad CRC is
rejected in `Strict` mode and reported as an error in `Recover` mode. Recovery
never invents events and never silently accepts a corrupt complete record.
