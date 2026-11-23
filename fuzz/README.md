# Fuzzers

Three deterministic mutation fuzzers exercise the production parsers without
libFuzzer or third-party runtimes. They are sanitizer-friendly: build with
`-DTEMPO_SANITIZE=ON` and any memory error aborts the run.

```sh
./build/fuzz_record --iterations 20000 [--seed N]
./build/fuzz_json   --iterations 20000 [--seed N]
./build/fuzz_http   --iterations 20000 [--seed N]
```

Each fuzzer is also registered in ctest as a 500-iteration smoke test.

## What is mutated

- **Record** starts from a valid record container and applies bit flips, byte
  overwrites, truncations, insertions, deletions, appended garbage and
  header-magic-prefixed random bodies. Every mutant is parsed in strict and
  recovery modes. When a record is valid, the hash must match recomputation,
  events must fit limits, and re-serialization must load identically.
- **JSON** starts from five valid documents and mutates characters, truncates,
  inserts, deletes or generates random bytes. Accepted values must survive
  `parse(dump(value))` with the same canonical dump; rejected values must carry
  a non-empty error.
- **HTTP** starts from valid GET/POST requests (with bodies and tokens) and
  mutates them. Complete requests must have sane paths, consumed sizes and body
  limits; errors must carry a 4xx/5xx status and message; serialization must
  retain the body length.

The fuzzers stop on the first invariant violation with a non-zero exit code.
They are not coverage-guided campaigns and do not prove the absence of bugs; see
`../LIMITATIONS.md`. Recorded 20,000-iteration results are in
`../docs/VERIFICATION.md`.
