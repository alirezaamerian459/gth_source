#include "gth/storage/SmartHealth.h"

#include <algorithm>
#include <format>

namespace gth::storage {

namespace {

constexpr std::size_t kSmartSectorBytes = 512;
constexpr std::size_t kSmartHeaderBytes = 2;
constexpr std::size_t kSmartEntryBytes = 12;
constexpr std::size_t kSmartEntries = 30;

// A value of 0, or 254/255, is how a drive says "this attribute isn't measured" - it must never be compared
// against a threshold as if it were a reading.
bool isMeasured(unsigned char value) { return value >= 1 && value <= 253; }

const char* attributeName(unsigned char id) {
    switch (id) {
        case 1: return "Read error rate";
        case 3: return "Spin-up time";
        case 5: return "Reallocated sectors";
        case 7: return "Seek error rate";
        case 10: return "Spin retries";
        case 177: return "Wear leveling";
        case 179: return "Used reserved blocks";
        case 181: return "Program fail count";
        case 182: return "Erase fail count";
        case 184: return "End-to-end errors";
        case 187: return "Uncorrectable errors";
        case 194: return "Temperature";
        case 196: return "Reallocation events";
        case 197: return "Pending sectors";
        case 198: return "Uncorrectable sectors";
        case 202: return "Lifetime remaining";
        case 231: return "SSD life left";
        case 232: return "Spare blocks left";
        case 233: return "Media wearout";
        default: return "Attribute";
    }
}

const SmartAttribute* find(const std::vector<SmartAttribute>& attributes, unsigned char id) {
    for (const auto& attribute : attributes) {
        if (attribute.id == id) return &attribute;
    }
    return nullptr;
}

// Tracks the biggest single deduction so the detail names the real culprit.
struct Scorer {
    int percent = 100;
    int biggest = 0;
    std::string detail;

    void deduct(int points, const std::string& reason) {
        if (points <= 0) return;
        percent -= points;
        if (points > biggest) {
            biggest = points;
            detail = reason;
        }
    }
    // A hard ceiling: the disk says it is failing, or a life counter says so.
    void cap(int ceiling, const std::string& reason) {
        if (ceiling < percent) {
            percent = ceiling;
            biggest = 1000;  // a cap outranks any deduction as the stated reason
            detail = reason;
        }
    }
};

// Counts of sectors the drive could not read or had to swap out. One or two is a warning sign, a growing pile is
// a failing disk; the bands follow how these counters are read in practice.
int sectorCountPenalty(unsigned long long count, int fewPoints, int someRange, int somePoints, int manyPoints) {
    if (count == 0) return 0;
    if (count <= 2) return fewPoints;
    if (count <= static_cast<unsigned long long>(someRange)) return somePoints;
    return manyPoints;
}

}  // namespace

std::vector<SmartAttribute> parseAtaSmartAttributes(const unsigned char* data, std::size_t bytes) {
    std::vector<SmartAttribute> attributes;
    if (data == nullptr || bytes < kSmartSectorBytes) return attributes;

    for (std::size_t i = 0; i < kSmartEntries; ++i) {
        const unsigned char* entry = data + kSmartHeaderBytes + i * kSmartEntryBytes;
        if (entry[0] == 0) continue;

        SmartAttribute attribute;
        attribute.id = entry[0];
        attribute.flags = static_cast<unsigned short>(entry[1] | (entry[2] << 8));
        attribute.current = entry[3];
        attribute.worst = entry[4];
        for (int b = 5; b >= 0; --b) attribute.raw = (attribute.raw << 8) | entry[5 + b];
        attributes.push_back(attribute);
    }
    return attributes;
}

void applyAtaSmartThresholds(std::vector<SmartAttribute>& attributes, const unsigned char* data, std::size_t bytes) {
    if (data == nullptr || bytes < kSmartSectorBytes) return;

    for (std::size_t i = 0; i < kSmartEntries; ++i) {
        const unsigned char* entry = data + kSmartHeaderBytes + i * kSmartEntryBytes;
        if (entry[0] == 0) continue;
        for (auto& attribute : attributes) {
            if (attribute.id == entry[0]) attribute.threshold = entry[1];
        }
    }
}

std::optional<int> ataTemperatureCelsius(const std::vector<SmartAttribute>& attributes) {
    for (const unsigned char id : {static_cast<unsigned char>(194), static_cast<unsigned char>(190)}) {
        if (const auto* attribute = find(attributes, id)) {
            // The current Celsius reading sits in the first raw byte; a raw value that isn't a temperature on this
            // drive is caught by the plausibility bound and reads as unknown rather than a nonsense number.
            const int celsius = static_cast<int>(attribute->raw & 0xFF);
            if (celsius > 0 && celsius < 100) return celsius;
        }
    }
    return std::nullopt;
}

HealthAssessment assessAtaHealth(const std::vector<SmartAttribute>& attributes, std::optional<bool> solidState) {
    Scorer score;

    // 1. The drive's own verdict: a pre-failure attribute that has fallen to its failure threshold means "replace now".
    for (const auto& attribute : attributes) {
        if (attribute.preFail() && attribute.threshold > 0 && isMeasured(attribute.current) &&
            attribute.current <= attribute.threshold) {
            score.cap(10, std::string(attributeName(attribute.id)) + " has reached its failure threshold");
        }
    }

    // 2. Sectors that could not be read or had to be replaced. Raw counters (low 32 bits: some vendors pack extra data above).
    const auto counter = [&](unsigned char id) -> unsigned long long {
        const auto* attribute = find(attributes, id);
        return attribute ? (attribute->raw & 0xFFFFFFFFull) : 0;
    };
    if (const auto reallocated = counter(5)) {
        // A few remapped sectors are normal wear; hundreds mean the surface is failing.
        int points = 55;
        if (reallocated <= 5) {
            points = 8;
        } else if (reallocated <= 20) {
            points = 15;
        } else if (reallocated <= 100) {
            points = 25;
        } else if (reallocated <= 500) {
            points = 40;
        }
        score.deduct(points, std::format("Reallocated sectors: {}", reallocated));
    }
    if (const auto pending = counter(197)) {
        score.deduct(sectorCountPenalty(pending, 10, 20, 20, 35), std::format("Pending sectors: {}", pending));
    }
    if (const auto uncorrectable = counter(198)) {
        score.deduct(sectorCountPenalty(uncorrectable, 10, 20, 20, 35), std::format("Uncorrectable sectors: {}", uncorrectable));
    }
    if (const auto reported = counter(187)) {
        score.deduct(reported <= 5 ? 10 : 25, std::format("Uncorrectable errors: {}", reported));
    }
    if (const auto endToEnd = counter(184)) {
        score.deduct(20, std::format("End-to-end errors: {}", endToEnd));
    }
    if (const auto spinRetries = counter(10)) {
        score.deduct(10, std::format("Spin retries: {}", spinRetries));
    }

    // 3. SSD wear: these attributes report the life left directly as their normalised value (100 = new).
    const bool readLifeCounters = solidState.value_or(true);
    for (const unsigned char id : {static_cast<unsigned char>(231), static_cast<unsigned char>(233), static_cast<unsigned char>(177),
                                   static_cast<unsigned char>(202), static_cast<unsigned char>(169)}) {
        const auto* attribute = readLifeCounters ? find(attributes, id) : nullptr;
        if (attribute == nullptr || attribute->current < 1 || attribute->current > 100) continue;
        if (attribute->current < 100) {
            score.cap(attribute->current, std::format("SSD life remaining: {}%", attribute->current));
        }
    }

    // A disk that isn't outright failing never reads below 20% from counters alone.
    if (score.biggest < 1000) score.percent = std::max(score.percent, 20);
    score.percent = std::clamp(score.percent, 0, 100);
    return {score.percent, score.percent < 100 ? score.detail : std::string()};
}

std::optional<NvmeHealth> parseNvmeHealthLog(const unsigned char* data, std::size_t bytes) {
    if (data == nullptr || bytes < 32) return std::nullopt;

    NvmeHealth health;
    health.criticalWarning = data[0];
    const int kelvin = data[1] | (data[2] << 8);
    if (kelvin > 273 && kelvin < 373) health.temperatureCelsius = kelvin - 273;
    health.availableSpare = data[3];
    health.spareThreshold = data[4];
    health.percentageUsed = data[5];
    return health;
}

HealthAssessment assessNvmeHealth(const NvmeHealth& health) {
    Scorer score;

    // Critical warning bits from the NVMe spec: 0 spare below threshold, 2 reliability degraded, 3 read-only,
    // 4 volatile-memory backup failed. (Bit 1 is a temperature warning: not a health problem in itself.)
    if (health.criticalWarning & 0x01) score.cap(10, "Available spare is below the drive's threshold");
    if (health.criticalWarning & 0x04) score.cap(10, "The drive reports its reliability is degraded");
    if (health.criticalWarning & 0x08) score.cap(10, "The drive has gone read-only");
    if (health.criticalWarning & 0x10) score.cap(10, "The drive's volatile-memory backup has failed");
    if (health.spareThreshold > 0 && health.availableSpare < health.spareThreshold) {
        score.cap(10, std::format("Available spare {}% is below the threshold", health.availableSpare));
    }

    // Percentage used is the drive's own estimate of rated endurance consumed.
    const int used = std::min<int>(health.percentageUsed, 100);
    if (used > 0) score.cap(100 - used, std::format("Drive wear: {}% of its rated life used", health.percentageUsed));

    score.percent = std::clamp(score.percent, 0, 100);
    return {score.percent, score.percent < 100 ? score.detail : std::string()};
}

}  // namespace gth::storage
