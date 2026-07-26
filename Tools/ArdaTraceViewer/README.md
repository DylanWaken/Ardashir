# Arda Trace Viewer

A dependency-light, offline trace viewer for ArdaTrace binary captures. Flask serves
the local application; rendering, filtering, zooming, and panning are implemented
with vanilla JavaScript and Canvas. No CDN or network service is used.

## Setup

From `Tools/ArdaTraceViewer`:

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
```

## Run

Open an empty viewer and upload a capture in the browser:

```powershell
python app.py
```

Or open a capture at startup:

```powershell
python app.py C:\captures\frame.ardatrace
```

Then visit `http://127.0.0.1:5000`. The default host is loopback-only and debug
mode is disabled. Use `--port` to choose another port. `--host` and `--debug`
must be supplied explicitly if their less-restrictive behavior is needed.

Uploads are limited to 256 MiB and parsed in memory. The HTTP application does
not expose an endpoint for browsing or opening arbitrary server-side paths.

## Controls

- Use the mouse wheel over the timeline to zoom around the pointer.
- Drag the timeline horizontally to pan.
- Hover a scope rectangle for its name, thread, duration, and IDs.
- Filter by thread or name; filters also update counters and aggregate statistics.
- Use **Reset view** to restore the full capture time range.

## Tests

```powershell
python -m unittest discover -s tests
```

The parser rejects invalid magic/version/endian fields, truncation, unknown
record types, missing capture-end records, oversized or invalid UTF-8 strings,
invalid references, duplicate IDs, non-finite counter values, parent cycles,
cross-thread parents, and scopes whose end precedes their start.
