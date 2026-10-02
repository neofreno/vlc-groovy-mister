#pragma once

namespace groovy_safe {
// Kernel references must disappear before backing memory is unmapped.
// Callbacks clear their owned handles on success, making partial retries safe.
template<class Detach, class Socket, class Umem, class Memory, class Object>
bool releaseXdpResources(Detach detach, Socket socket, Umem umem, Memory memory, Object object)
{
    if (!detach()) return false;
    socket();
    if (!umem()) return false;
    memory();
    object();
    return true;
}
}
