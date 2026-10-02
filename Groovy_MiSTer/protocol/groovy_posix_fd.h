#pragma once
#include <fcntl.h>

namespace groovy_safe {
inline bool closeOnExec(int fd)
{
    const int flags = fcntl(fd, F_GETFD);
    return flags >= 0 && fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}
}
