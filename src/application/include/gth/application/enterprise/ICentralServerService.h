#pragma once

#include <vector>

#include "gth/core/enterprise/RemoteNode.h"

namespace gth::application {

// The "Central Server" half of Milestone 14: listens for agent reports and
// keeps a catalog of known peer installations. Off by default - a desktop
// app doesn't become a server until the user explicitly starts it (Settings
// -> Enterprise, "Start Server").
class ICentralServerService {
public:
    virtual ~ICentralServerService() = default;

    virtual void start(int port) = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;

    virtual std::vector<core::RemoteNode> getAllNodes() = 0;
};

}  // namespace gth::application
