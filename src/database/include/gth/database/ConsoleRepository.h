#pragma once

#include "gth/core/devices/IConsoleRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

class ConsoleRepository : public core::IConsoleRepository {
public:
    explicit ConsoleRepository(Database& db);

    std::vector<core::Console> getAll() override;
    std::optional<core::Console> getById(const std::string& consoleId) override;
    std::optional<core::Console> getByIpAddress(const std::string& ipAddress) override;

    void add(const core::Console& console) override;
    void update(const core::Console& console) override;
    void remove(const std::string& consoleId) override;

private:
    Database& db_;
};

}  // namespace gth::database
