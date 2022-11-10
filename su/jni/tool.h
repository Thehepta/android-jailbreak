//
// Created by chic on 2022/8/25.
//

#ifndef SOCKET_TOOL_H
#define SOCKET_TOOL_H

#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <cerrno>
#include <sys/socket.h>
#include <thread>
#include <asm-generic/ioctls.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/un.h>

#define REQUESTOR_SOCKET  "d63138f231"


#define ERR_EXIT(m)         \
    do                      \
    {                       \
        perror(m);          \
        exit(EXIT_FAILURE); \
    } while (0);

volatile static int closing_time = 0;


static int read_int(int fd) {
    int val;
    int len = read(fd, &val, sizeof(int));
    if (len != sizeof(int)) {
        ERR_EXIT("unable to read_int");

        }
    return val;
}

static void write_int(int fd, int val) {
    int written = write(fd, &val, sizeof(int));
    if (written != sizeof(int)) {
        ERR_EXIT("unable to write int");
    }
}

static char* read_string(int fd) {
    int len = read_int(fd);
    if (len > PATH_MAX || len < 0) {
        ERR_EXIT("invalid string length");
    }
    char* val = static_cast<char *>(malloc(sizeof(char) * (len + 1)));
    if (val == NULL) {
        ERR_EXIT("unable to malloc string");
    }
    val[len] = '\0';
    int amount = read(fd, val, len);
    if (amount != len) {
        ERR_EXIT("unable to read string");
    }
    return val;
}

static void write_string(int fd, char* val) {
    int len = strlen(val);
    write_int(fd, len);
    int written = write(fd, val, len);
    if (written != len) {
        ERR_EXIT("unable to write string");
    }
}
static int recv_fd(int sockfd) {
    // Need to receive data from the message, otherwise don't care about it.
    char iovbuf;

    struct iovec iov = {
            .iov_base = &iovbuf,
            .iov_len  = 1,
    };

    char cmsgbuf[CMSG_SPACE(sizeof(int))];

    struct msghdr msg = {
            .msg_iov        = &iov,
            .msg_iovlen     = 1,
            .msg_control    = cmsgbuf,
            .msg_controllen = sizeof(cmsgbuf),
    };

    if (recvmsg(sockfd, &msg, MSG_WAITALL) != 1) {
        ERR_EXIT("unable to read fd");
    }

    // Was a control message actually sent?
    switch (msg.msg_controllen) {
        case 0:
            // No, so the file descriptor was closed and won't be used.
            return -1;
        case sizeof(cmsgbuf):
            // Yes, grab the file descriptor from it.
            break;
        default:
            ERR_EXIT("unable to read fd");
    }

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);

    if (cmsg             == NULL                  ||
        cmsg->cmsg_len   != CMSG_LEN(sizeof(int)) ||
        cmsg->cmsg_level != SOL_SOCKET            ||
        cmsg->cmsg_type  != SCM_RIGHTS) {
        error:
        ERR_EXIT("unable to read fd");
    }

    return *(int *)CMSG_DATA(cmsg);
}

static void send_fd(int sockfd, int fd) {
    // Need to send some data in the message, this will do.
    struct iovec iov = {
            .iov_base = (void *) "",
            .iov_len  = 1,
    };

    struct msghdr msg = {
            .msg_iov        = &iov,
            .msg_iovlen     = 1,
    };

    char cmsgbuf[CMSG_SPACE(sizeof(int))];

    if (fd != -1) {
        // Is the file descriptor actually open?
        if (fcntl(fd, F_GETFD) == -1) {
            if (errno != EBADF) {
                goto error;
            }
            // It's closed, don't send a control message or sendmsg will EBADF.
        } else {
            // It's open, send the file descriptor in a control message.
            msg.msg_control    = cmsgbuf;
            msg.msg_controllen = sizeof(cmsgbuf);

            struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);

            cmsg->cmsg_len   = CMSG_LEN(sizeof(int));
            cmsg->cmsg_level = SOL_SOCKET;
            cmsg->cmsg_type  = SCM_RIGHTS;

            *(int *)CMSG_DATA(cmsg) = fd;
        }
    }

    if (sendmsg(sockfd, &msg, 0) != 1) {
        error:
        ERR_EXIT("unable to send fd");
    }
}

static void *watch_sigwinch(void *data) {
    sigset_t winch;
    int sig;
    int master = ((int *)data)[0];
    int slave = ((int *)data)[1];

    sigemptyset(&winch);
    sigaddset(&winch, SIGWINCH);

    do {
        // Wait for a SIGWINCH
        sigwait(&winch, &sig);

        if (closing_time) break;

        // Get the new terminal size
        struct winsize w;
        if (ioctl(master, TIOCGWINSZ, &w) == -1) {
            continue;
        }

        // Set the new terminal size
        ioctl(slave, TIOCSWINSZ, &w);

    } while (1);

    free(data);
    return NULL;
}

static int watch_sigwinch_async(int master, int slave) {
    pthread_t watcher;
    int *files = (int *) malloc(sizeof(int) * 2);
    if (files == NULL) {
        return -1;
    }

    // Block SIGWINCH so sigwait can later receive it
    sigset_t winch;
    sigemptyset(&winch);
    sigaddset(&winch, SIGWINCH);
    if (sigprocmask(SIG_BLOCK, &winch, NULL) == -1) {
        free(files);
        return -1;
    }

    // Initialize some variables, then start the thread
    closing_time = 0;
    files[0] = master;
    files[1] = slave;
    int ret = pthread_create(&watcher, NULL, &watch_sigwinch, files);
    if (ret != 0) {
        free(files);
        errno = ret;
        return -1;
    }

    // Set the initial terminal size
    raise(SIGWINCH);
    return 0;
}





#endif //SOCKET_TOOL_H
