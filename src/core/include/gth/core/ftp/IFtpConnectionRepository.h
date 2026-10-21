#pragma once

#include <optional>
#include <string>
#include <vector>

#include "gth/core/ftp/FtpConnection.h"

namespace gth::core {

class IFtpConnectionRepository {
public:
    virtual ~IFtpConnectionRepository() = default;

    virtual std::vector<FtpConnection> getAll() = 0;
    virtual std::optional<FtpConnection> getById(const std::string& connectionId) = 0;

    virtual void add(const FtpConnection& connection) = 0;
    virtual void update(const FtpConnection& connection) = 0;
    virtual void remove(const std::string& connectionId) = 0;
};

}  // namespace gth::core
