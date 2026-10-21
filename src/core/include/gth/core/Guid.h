#pragma once

#include <string>

namespace gth::core {

// Thin wrapper around the Windows RPC UUID API - the one OS dependency Core
// takes on, in exchange for every entity ID being a normal std::string
// everywhere else in the codebase.
namespace Guid {
    std::string newGuid();
}

}  // namespace gth::core
