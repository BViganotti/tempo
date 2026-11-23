# Record format (v1)

Tempo records are little-endian, self-describing binary files. All integers are
unsigned. Reads are bounded by `include/tempo/limits.hpp`; malformed input fails
cleanly, and recovery never invents events.

## File layout

```
header (40 bytes)
body
  metadata section
  string table
  events
  event index
  event-region byte count
body CRC32 (4 bytes)
footer (20 bytes)
```

Total file size for a complete record is `40 + body_size + 24`. `body_size` in
the header excludes the trailing body CRC and footer. A file larger than 64 MiB
is rejected before parsing.

## Header (40 bytes)

| offset | size | field |
|---:|---:|---|
| 0 | 4 | magic bytes `TMPO` |
| 4 | 2 | format version (1) |
| 6 | 2 | flags (bit 0 = complete) |
| 8 | 4 | CRC32 of bytes 0..7 |
| 12 | 8 | body offset (always 40) |
| 20 | 8 | body size |
| 28 | 4 | body CRC32 |
| 32 | 4 | reserved (0) |
| 36 | 4 | CRC32 of bytes 0..35 |

## Body sections

### 1. Metadata

`count:u32`, then each field as `key_len:u32`, `key`, `value_len:u32`, `value`.
All strings are UTF-8 by convention. Known keys are `scenario`, `tool`, `seed`,
`created`, `start_ns`. Unknown keys are preserved in `RecordMeta::attributes`.
Limits: 64 fields, key ≤ 64 bytes, value ≤ 4,096 bytes, total ≤ 64 KiB.

### 2. String table

`count:u32`, then each string as `len:u32`, `bytes`. Index 0 is the empty string
in files written by Tempo. Limits: 200,000 strings, 65,536 bytes each, 16 MiB
total.

### 3. Events

`count:u32` (≤ 1,000,000), then each event:

| size | field |
|---:|---|
| 1 | event type (stable numeric id, see `docs/EVENT_TYPES.md`) |
| 1 | event flags (0) |
| 2 | task id (0 = system) |
| 8 | a |
| 8 | b |
| 8 | c |
| 8 | d |
| 4 | string ref 1 |
| 4 | string ref 2 |
| 4 | payload length (≤ 65,536) |
| N | payload bytes |
| 4 | CRC32 of the event record excluding this field |

String references must be less than the string-table count. Unknown type ids are
preserved as their raw numeric value so a newer file remains inspectable; the
display name is `?`.

### 4. Event index

`count:u32`, then `offset:u32` per entry, relative to the first event record. One
entry is written for events 0, 64, 128, ... The current reader validates bounds
and skips the index; it is part of the format for future random access.

### 5. Event-region byte count

`u64` equal to the exact number of bytes occupied by the event records. A
mismatch is a format error (strict) or a truncation warning (recovery).

## Trailer

After the body:

| size | field |
|---:|---|
| 4 | body CRC32 (repeated) |
| 4 | footer magic `ENDT` |
| 8 | event count |
| 4 | body CRC32 (repeated again) |
| 4 | CRC32 of the previous 16 footer bytes |

## Integrity and recovery

- Every event has its own CRC32, so a single damaged event stops parsing at that
  point without discarding earlier valid events.
- The body CRC covers metadata, strings, events, index and region size.
- The footer repeats the body CRC and count, and has its own checksum, catching
  appended garbage and truncation.
- `LoadMode::Strict` requires a complete footer, the complete flag, matching
  CRCs and exact file size.
- `LoadMode::Recover` parses the valid prefix of a truncated or damaged file.
  It sets `complete=false`, `recovered=true`, a human warning, and preserves only
  complete events. A bad header magic/version/header CRC is fatal in both modes
  because the structure can no longer be trusted.

## Determinism hash

The in-memory `Record::hash` is FNV-1a 64 over the canonical encoding of every
event: type, task, `a`–`d`, and length-prefixed `s1`, `s2`, `bytes`. The hash is
recomputed on load. It is independent of the on-disk string indices, so a record
that is loaded and re-saved has the same hash and identical bytes (the writer
assigns the same indices deterministically).
