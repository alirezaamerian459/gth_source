#include "gth/core/Guid.h"

#include <rpc.h>
#pragma comment(lib, "rpcrt4.lib")

namespace gth::core::Guid {

std::string newGuid() {
    UUID uuid{};
    const RPC_STATUS createStatus = ::UuidCreate(&uuid);
    if (createStatus != RPC_S_OK && createStatus != RPC_S_UUID_LOCAL_ONLY) return {};

    RPC_CSTR rpcStr = nullptr;
    if (::UuidToStringA(&uuid, &rpcStr) != RPC_S_OK || rpcStr == nullptr) return {};

    std::string result(reinterpret_cast<char*>(rpcStr));
    ::RpcStringFreeA(&rpcStr);
    return result;
}

}  // namespace gth::core::Guid
