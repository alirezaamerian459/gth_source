#pragma once

#include "gth/core/reports/ArchiveHealthReport.h"

namespace gth::application {

class IReportService {
public:
    virtual ~IReportService() = default;

    virtual core::reports::ArchiveHealthReport generate() = 0;
};

}  // namespace gth::application
