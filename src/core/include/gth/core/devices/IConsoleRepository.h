#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gth/core/devices/Console.h"

namespace gth::core {

class IConsoleRepository {
public:
    virtual ~IConsoleRepository() = default;

    virtual std::vector<Console> getAll() = 0;
    virtual std::optional<Console> getById(const std::string& consoleId) = 0;
    virtual std::optional<Console> getByIpAddress(const std::string& ipAddress) = 0;

    virtual void add(const Console& console) = 0;
    virtual void update(const Console& console) = 0;
    virtual void remove(const std::string& consoleId) = 0;
};

}  // namespace gth::core
