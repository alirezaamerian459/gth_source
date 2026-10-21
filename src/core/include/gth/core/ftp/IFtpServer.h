#pragma once

#include "gth/core/ftp/FtpServerConfig.h"

namespace gth::core {

// A minimal FTP *server* - the reverse of IFtpClient. Exposes one local
// folder (FtpServerConfig::rootPath) for browsing/upload/download by any
// FTP client on the LAN, with no authentication (by explicit design - see
// WinsockFtpServer). Implementations own their own listening socket and
// worker threads; start()/stop() are idempotent, matching every other
// explicit-opt-in background service in this codebase (UpdateSchedulerService,
// TransferQueueService, CentralServerListener, ...).
class IFtpServer {
public:
    virtual ~IFtpServer() = default;

    virtual void start(const FtpServerConfig& config) = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    // The port actually bound (equal to FtpServerConfig::port once running,
    // 0 when not running).
    virtual int port() const = 0;
};

}  // namespace gth::core
