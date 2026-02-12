# Simulator Integration Modules

## Module Layout

The `sim/` namespace contains PiTrac's simulator bridges:

- `sim/common/` – shared simulator abstractions such as `GsSimInterface` and socket utilities.
- `sim/gspro/` – GSPro-specific IPC implementation.

Future phases can expand this area with unit tests and additional simulator adapters.
