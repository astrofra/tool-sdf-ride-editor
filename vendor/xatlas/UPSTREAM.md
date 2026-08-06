# xatlas Upstream Tracking

- Source repository: `https://github.com/jpcy/xatlas`
- Vendored on: `2026-08-06`
- Upstream commit: `f700c7790aaa030e794b52ba7791a05c085faf0c`

Vendored files:

- `xatlas.cpp`
- `xatlas.h`
- `LICENSE`

Local integration notes:

- `xatlas` is linked directly into `sdf_core`
- the vendored translation unit is compiled with `XA_DEBUG=0`
- warnings are suppressed for the vendored translation unit only
