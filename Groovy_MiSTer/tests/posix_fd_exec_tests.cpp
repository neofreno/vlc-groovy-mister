#include "../protocol/groovy_posix_fd.h"
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cassert>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc == 2) return fcntl(200, F_GETFD) == -1 && errno == EBADF ? 0 : 1;
    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, pair) == 0);
    assert(dup2(pair[0], 200) == 200);
    assert(!groovy_safe::closeOnExec(-1));
    assert(groovy_safe::closeOnExec(200));
    assert(groovy_safe::closeOnExec(200));
    assert(fcntl(200, F_GETFD) & FD_CLOEXEC);
    const pid_t child = fork();
    assert(child >= 0);
    if (!child) { execl(argv[0], argv[0], "check", nullptr); _exit(2); }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(fcntl(200, F_GETFD) >= 0); // Only the exec'ed process loses the fd.
    close(200); close(pair[0]); close(pair[1]);
    std::puts("PASS: production CLOEXEC helper prevents socket inheritance across real fork/exec");
}
