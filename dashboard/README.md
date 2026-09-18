# Flash usage dashboard

Generate a self-contained HTML report from an existing ESP-IDF build:

```powershell
.\dashboard\generate-flash-dashboard.ps1
```

The default input is `build/` and the report is written to
`dashboard/output/flash-usage.html`. Override either location when comparing
named or archived builds:

```powershell
.\dashboard\generate-flash-dashboard.ps1 `
    -BuildDirectory .\build-release `
    -OutputDirectory .\dashboard\output\release
```

The script reads `flasher_args.json`, the compiled binary partition table,
`size-components.csv`, `project_description.json`, and the flashed binary
images. The report includes a proportional SVG flash map, per-partition image
utilisation, the largest linked archives, component source locations, and a
project/managed-component/ESP-IDF source breakdown.

For LittleFS, utilisation is estimated from the staged source files so the
fixed-size filesystem image does not misleadingly appear to contain only useful
data. Filesystem metadata and allocation overhead are not included, so retain a
safety margin when reducing that partition.

Run `idf.py build` first. The generated dashboard is intentionally ignored by
Git; the script and this documentation are the reproducible sources.
