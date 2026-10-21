#pragma once

#include "gth/application/enterprise/ICentralServerService.h"
#include "gth/core/enterprise/ICentralServerListener.h"
#include "gth/core/enterprise/IRemoteNodeRepository.h"

namespace gth::application {

class CentralServerService : public ICentralServerService {
public:
    CentralServerService(core::IRemoteNodeRepository& repository, core::ICentralServerListener& listener)
        : repository_(repository), listener_(listener) {}

    void start(int port) override;
    void stop() override { listener_.stop(); }
    bool isRunning() const override { return listener_.isRunning(); }

    std::vector<core::RemoteNode> getAllNodes() override { return repository_.getAll(); }

private:
    core::IRemoteNodeRepository& repository_;
    core::ICentralServerListener& listener_;
};

}  // namespace gth::application
