#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gth/core/storage/PhysicalDiskHealth.h"

// Turns a disk's own SMART data into a health percentage.
//
// Windows' storage API only says Healthy / Warning / Unhealthy. The disk itself knows much more: counters of
// reallocated and unreadable sectors, and on SSDs how much of the flash's rated life is used up. This reads
// those (ATA SMART attributes; the NVMe health log page) and scores them with the rules written out in
// SmartHealth.cpp. It is an estimate built from documented attribute meanings, not a vendor's private formula,
// so it will not match another tool's number exactly - but every point taken off has a stated reason.
namespace gth::storage {

struct SmartAttribute {
    unsigned char id = 0;
    unsigned short flags = 0;  // bit 0: a failing value means the drive is about to fail (pre-failure attribute)
    unsigned char current = 0;
    unsigned char worst = 0;
    unsigned long long raw = 0;  // the 6 raw bytes, little-endian
    unsigned char threshold = 0; // 0 = none known
    std::string name;             // populated by text-report parsers when available
    std::string status;           // vendor-reported status, e.g. Good/Warning/Bad

    bool preFail() const { return (flags & 0x0001) != 0; }
};

// The text format emitted by QNAP's qcli_hdd command.  It is intentionally
// parsed as an input format only: no credentials or SSH behavior belong in
// this low-level library.  Missing or malformed fields stay absent.
struct QnapSmartReport {
    std::optional<int> bay;
    std::string status;
    std::vector<SmartAttribute> attributes;
};

struct HealthAssessment {
    int percent = 100;
    std::string detail;  // why it isn't 100 (the biggest factor); empty when nothing is wrong
};

// The 512-byte SMART READ DATA sector: 30 twelve-byte entries after a 2-byte revision. Unused (id 0) entries are skipped.
std::vector<SmartAttribute> parseAtaSmartAttributes(const unsigned char* data, std::size_t bytes);

// Parse one QNAP qcli_hdd report, including lines such as:
// `194 Temperature_Celsius 042 057 000 42 Good`.
std::optional<QnapSmartReport> parseQnapSmartReport(std::string_view text);

// The 512-byte SMART READ THRESHOLDS sector; fills in each attribute's threshold.
void applyAtaSmartThresholds(std::vector<SmartAttribute>& attributes, const unsigned char* data, std::size_t bytes);

// Drive temperature in Celsius from attribute 194 (or 190), when it holds a plausible reading.
std::optional<int> ataTemperatureCelsius(const std::vector<SmartAttribute>& attributes);

// Pull the counters that are meaningful across the ATA and QNAP text forms.
// Raw values remain in vendor-reported units; only the temperature and health
// assessment apply interpretation rules.
gth::core::PhysicalDiskHealth::SmartMetrics smartMetrics(
    const std::vector<SmartAttribute>& attributes);

// Convert a manually supplied QNAP bay report into the same disk-level model
// used by the Windows provider. The SMART score remains an estimate from the
// attributes; the QNAP status is preserved separately as the authoritative
// vendor label.
gth::core::PhysicalDiskHealth physicalDiskHealthFromQnapReport(const QnapSmartReport& report);

// `solidState`: true for an SSD, false for a spinning disk, nullopt when unknown. The SSD life counters (attributes
// 231/233/177/202/169) are only read on a disk that is or may be an SSD - on a hard disk some of those numbers mean
// something else entirely.
HealthAssessment assessAtaHealth(const std::vector<SmartAttribute>& attributes, std::optional<bool> solidState = std::nullopt);

// NVMe SMART / Health Information log page (512 bytes).
struct NvmeHealth {
    unsigned char criticalWarning = 0;
    std::optional<int> temperatureCelsius;
    unsigned char availableSpare = 100;  // percent
    unsigned char spareThreshold = 0;    // percent
    unsigned char percentageUsed = 0;    // of the rated endurance; may exceed 100
};

std::optional<NvmeHealth> parseNvmeHealthLog(const unsigned char* data, std::size_t bytes);
HealthAssessment assessNvmeHealth(const NvmeHealth& health);

}  // namespace gth::storage
