# ADR-0005: Preserve QNAP SMART telemetry without guessing vendor units

Status: accepted

## Context

QNAP eight-bay reports expose a per-bay SMART table with normalized values, thresholds, raw counters, and a vendor status. A network share exposes capacity and free space but not the physical disk's SMART data. The same raw counter can have different units on different drive families.

## Decision

- Keep selected SMART counters in `core::PhysicalDiskHealth::SmartMetrics` and retain their raw values.
- Parse the QNAP `qcli_hdd` text shape as a read-only input format, including bay number and vendor status.
- Calculate temperature from attributes 194/190 and health from the existing documented SMART assessment; preserve the QNAP status separately.
- For external disks whose USB/SATA bridge does not pass ATA SMART through, query Windows' read-only `StorageDeviceTemperatureProperty` as a temperature fallback. If the device reports no valid sensor value, keep the temperature unknown.
- Refresh reachable NAS/SMB capacity and free space through `GetDiskFreeSpaceExW` during storage rescans.
- Leave unavailable values unknown. Do not convert LBA counters to bytes without an explicit drive-sector/unit definition, and do not store or automate NAS credentials.

## Consequences

The Storage tab can display useful per-bay telemetry without falsely presenting vendor raw values as byte counts. A future QNAP connector can reuse the parser, but SSH/API authentication remains a separate approved feature.
