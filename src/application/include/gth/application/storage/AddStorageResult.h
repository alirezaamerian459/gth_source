#pragma once

#include <string>

namespace gth::application {

struct AddStorageResult {
    bool success = false;
    std::string errorMessage;
    std::string storageId;
};

}  // namespace gth::application
