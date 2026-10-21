#pragma once

#include <string>

namespace gth::application {

// The "Agent" half of Milestone 14: periodically pushes a summary of this
// installation's own archive to a Central Server the user has configured.
// Off by default, same as ICentralServerService - nothing dials out on its
// own. start() sends one report immediately (so the UI gets instant
// success/failure feedback) and then repeats every intervalSeconds until
// stop().
class IAgentService {
public:
    virtual ~IAgentService() = default;

    // Returns whether the first, immediate report succeeded.
    virtual bool start(const std::string& serverAddress, int port, const std::string& nodeName,
                        int intervalSeconds) = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;

    virtual const std::string& localNodeId() const = 0;
};

}  // namespace gth::application
