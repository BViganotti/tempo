# Benchmarks

`tempo_bench` measures the shipped code on the machine it runs on:

- serialize and strict-load a synthetic 20,000-event record;
- compute the determinism hash;
- build and dump a 1,000-event JSON page;
- save/load a `.tmpo` file through the real atomic writer;
- record the built-in counter race 50 times;
- replay it 500 times (checking identity each time).

Run:

```sh
./build/tempo_bench
```

Every number is printed at runtime; none is compiled in. The benchmark creates
and removes one temporary file under the system temp directory. It is not a
formal statistical benchmark: it reports aggregate ops/s and MiB/s for a single
run so performance regressions are visible. Results from the final verification
run are in `../docs/VERIFICATION.md` and `../FINAL_REPORT.md`.

What the numbers do and do not show:

- replay of the small demo is microseconds; the scheduler is not the bottleneck;
- record serialization/loading throughput is dominated by CRC and string
  handling, not I/O, for the synthetic record;
- JSON page construction is measured to bound UI response cost;
- the file benchmark includes `fsync` and rename, so it reflects the durability
  path rather than page-cache-only writes.
