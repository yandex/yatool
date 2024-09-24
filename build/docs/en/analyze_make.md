## ya analyze-make timeline

The `ya analyze-make timeline` command is intended for analyzing and recreating a timeline of events that occurred in a build process launched with `ya make`.

It visualizes what exactly happened and how long it took to help optimize and debug the build process.

### Command syntax

`ya analyze-make timeline [options]`

### Functionality

1. Data collection and analysis:
- Running `ya make` always creates an `event log`, which is saved to `~/.ya/evlogs/`.
- The `ya analyze-make timeline` command uses these logs to build a `trace` for further analysis.

2. Output data formats:
- By default, `ya analyze-make timeline` generates traces that can be displayed in Yandex Browser (via `browser://tracing`) and Chrome (via `chrome://tracing/`).
- Additionally supported is a format compatible with the `matplotlib` library for visualization.

3. Selecting a file for analysis:
- By default, the command uses the last trace from `~/.ya/evlogs/` as of the current date.
- If necessary, you can explicitly specify a file you want to analyze by using the `--evlog` option.

**Example**
```bash translate=no
$ ./ya analyze-make timeline
Open about://tracing in Chromium and load 14-27-47.ndpzv5xlrled702w.evlog.json file.
```
### Options

- `--evlog=ANALYZE_EVLOG_FILE`: Analyze a log from the file.
- `--format=OUTPUT_FORMAT`: Output format (by default, `chromium_trace`).
- `--plot`: `plot` output format (`matplotlib`).
- `-h`, `--help`: Help.
