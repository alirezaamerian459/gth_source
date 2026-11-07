#pragma once

#include <string>

namespace gth::application {

struct StorageCategoryResult {
    bool success = false;
    std::string categoryId;
    std::string errorMessage;
};

}  // namespace gth::application
