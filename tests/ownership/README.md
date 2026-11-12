# Ownership regressions

`run.sh` builds `engine_harness.cpp` with ASan/UBSan against the real plugin
sources (`4DPlugin-GS.cpp`, `ARRAY_TEXT.cpp`, `C_LONGINT.cpp`, the dormant
`support/4DPlugin-JSON.cpp`) and the bundled, unmodified 4D Plugin API
(`4DPluginAPI.c`). Only `gCall4D` (the 4D engine) and the `gsapi_*`
Ghostscript entry points are replaced by synthetic stand-ins that track every
engine allocation, so leaks, double frees and reads of failed results are
reported exactly.

```sh
tests/ownership/run.sh . fixed          # every test must pass

# same harness, immutable baseline: named defects fail, controls pass
B=$(mktemp -d); git archive <base-sha> | tar -x -C "$B"
tests/ownership/run.sh "$B" baseline
```

`support/4DPlugin-JSON.cpp` is not part of any build (Xcode or Visual
Studio); its tests guard the copy-forward template, not the shipped plugin.
The `control_cmd_gs_*` and `control_array_text_*` tests cover the shipped
`GS` command and the TEXT ARRAY contract (reading borrows,
`PA_SetStringInArray` transfers ownership to the array).

This is a macOS-only model of the engine boundary. It does not replace
validation in 4D on Windows or macOS with the real Ghostscript library.
