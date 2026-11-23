# Embeddable example

This example uses Tempo as a library with no UI and no third-party code. It:

1. defines a small cooperative scenario with two racing tasks,
2. records it through a `RunHost` that supplies file, environment and random
   results,
3. writes a portable `.tmpo` record,
4. reloads the record and replays it 100 times, checking hash stability.

Build and run from the repository root:

```sh
cmake --build build --target tempo_example
./build/tempo_example /tmp/tempo-example.tmpo
```

The output prints the event count, the determinism hash, and whether all 100
replays were bit-identical. Delete the `.tmpo` file when you are done.

The example deliberately implements `RunHost` directly instead of using
`SystemHost`, to show that a host can be fully in-memory and deterministic.
