#include "gth/core/Guid.h"

#include <rpc.h>
#pragma comment(lib, "rpcrt4.lib")

namespace gth::core::Guid {

std::string newGuid() {
    UUID uuid;
    ::UuidCreate(&uuid);

    RPC_CSTR rpcStr = nullptr;
    ::UuidToStringA(&uuid, &rpcStr);

    std::string result(reinterpret_cast<char*>(rpcStr));
    ::RpcStringFreeA(&rpcStr);
    return result;
}

}  // namespace gth::core::Guid
