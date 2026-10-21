#pragma once

#include <string>
#include <string_view>

namespace gth::core {

enum class ScanJobStatus {
    Unknown = 0,
    Running = 1,
    Completed = 2,
    Failed = 3
};

std::string toString(ScanJobStatus status);
ScanJobStatus scanJobStatusFromString(std::string_view text);

}  // namespace gth::core
