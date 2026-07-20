# debufr-profile-extractor

`debufr-profile-extractor` extracts selected vertical-profile variables from human-readable NOAA/NCEP `debufr` output and writes them to CSV.

## Purpose

The tool scans `debufr` text output line by line, finds requested radiosonde profiles by `RPID`, keeps each `UARLV` replication separate by default, and writes one CSV row per pressure level (or per replication when duplicate pressures are preserved).

## Dependencies

- C++17 compiler
- CMake 3.20+
- `yaml-cpp`

## Building on Linux

```bash
git clone <repository>
cd debufr-profile-extractor

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build -j
```

If `yaml-cpp` is not installed system-wide, either install it first or allow CMake to fetch it:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DDEBUFR_PROFILE_EXTRACTOR_FETCH_YAML_CPP=ON
```

## Installing

```bash
cmake --install build --prefix "$HOME/.local"
```

If needed, add the install location to `PATH`:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

## yaml-cpp installation

Ubuntu or Debian:

```bash
sudo apt-get install libyaml-cpp-dev
```

On HPC systems without administrator access, load compiler/CMake modules as needed and either point CMake at a local `yaml-cpp` install or configure with `-DDEBUFR_PROFILE_EXTRACTOR_FETCH_YAML_CPP=ON` so CMake downloads and builds `yaml-cpp` automatically.

## Running

```bash
debufr-profile-extractor \
  --config config/profile_extractor.yaml
```

You can also run directly from the build tree:

```bash
./build/debufr-profile-extractor \
  --config config/profile_extractor.yaml
```

Additional commands:

```bash
debufr-profile-extractor --help
debufr-profile-extractor --version
debufr-profile-extractor --validate-config config/profile_extractor.yaml
```

## Configuration overview

The sample configuration lives at `config/profile_extractor.yaml`.

Supported options include:

- input file path
- station list
- message-type filter
- optional start/end time filtering
- configurable variable mnemonics and output column names
- optional metadata mnemonics
- pressure and temperature unit conversion
- optional computed `u_wind`/`v_wind`
- duplicate-pressure handling (`preserve` or `merge`)
- optional merging of levels across messages
- CSV organization (`one_file`, `one_file_per_station`, `one_file_per_profile`)

## Output interpretation

- By default, one CSV row corresponds to one `UARLV` replication.
- Pressure is associated only with variables read while the parser is inside the same `UARLV REPLICATION #` block.
- Duplicate pressure rows are therefore legitimate and are preserved by default.
- Setting `duplicate_pressure_handling: merge` combines complementary rows at the same pressure within a profile and adds a `source_levels` column such as `1;7`.
- When `compute_wind_components: true`, output columns `u_wind` and `v_wind` are added. Original `UWND`/`VWND` values take precedence; otherwise the tool computes them from `WDIR` and `WSPD` using meteorological direction.

## Parser logic

The parser is a line-oriented state machine:

1. It recognizes the start of each BUFR message from `Found BUFR message` or `BUFR message #`.
2. It records message metadata such as `MESSAGE TYPE`, `RPID`, coordinates, and observation time components.
3. It enters profile-level parsing only after `{UARLV}` is seen.
4. It starts a fresh level only on `UARLV REPLICATION #`.
5. While inside that active level, it assigns `PRLC` and configured mnemonics to that level only.
6. It finalizes the active level whenever another replication starts, a new profile starts, a new message starts, or EOF is reached.

That finalization rule is what prevents accidental mixing between adjacent replications: a variable encountered after replication `#2` has started can only be stored in level 2, never in level 1.

## Tests

The project includes GoogleTest-based parser and CSV tests covering:

- `RPID` extraction
- message type extraction
- pressure/temperature extraction
- pressure/wind extraction
- `MISSING` handling
- spacing/alias robustness
- duplicate-pressure preserve and merge behavior
- station filtering and missing-station reporting
- unit conversions
- computed wind components
- EOF finalization
- replication-boundary isolation

Build and run tests with:

```bash
cmake -S . -B build -DBUILD_TESTING=ON -DDEBUFR_PROFILE_EXTRACTOR_FETCH_YAML_CPP=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Troubleshooting

### Station not found

Check that `selection.stations` matches the exact `RPID` text in the input file, including leading zeros or embedded spaces.

### No `UARLV` sequence found

Only data inside `{UARLV}` / `UARLV REPLICATION #` blocks are treated as profile levels. If the requested mnemonic appears elsewhere, it is ignored on purpose.

### Malformed YAML

Run:

```bash
debufr-profile-extractor --validate-config config/profile_extractor.yaml
```

### `yaml-cpp` not found by CMake

Install `libyaml-cpp-dev`, point CMake to your local install, or rebuild with `-DDEBUFR_PROFILE_EXTRACTOR_FETCH_YAML_CPP=ON`.

### Requested mnemonic appears outside `UARLV`

The parser intentionally ignores profile variables that are not inside an active `UARLV` replication so variables cannot be attached to the wrong pressure.

### Output directory is not writable

Set `output.directory` to a writable path or adjust filesystem permissions before running the extractor.
