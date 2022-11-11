

#include <climits>
#include <string>
#include <sys/wait.h>
#include "su_client.h"
#include "pts.h"


#define ATTY_IN     1
#define ATTY_OUT    2
#define ATTY_ERR    4



// List of signals which cause process termination
static int quit_signals[] = { SIGALRM, SIGHUP, SIGPIPE, SIGQUIT, SIGTERM, SIGINT, 0 };

static void sighandler(int sig) {
    restore_stdin();

    // Assume we'll only be called before death
    // See note before sigaction() in set_stdin_raw()
    //
    // Now, close all standard I/O to cause the pumps
    // to exit so we can continue and retrieve the exit
    // code
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Put back all the default handlers
    struct sigaction act;
    int i;

    memset(&act, '\0', sizeof(act));
    act.sa_handler = SIG_DFL;
    for (i = 0; quit_signals[i]; i++) {
        if (sigaction(quit_signals[i], &act, NULL) < 0) {
            printf("Error removing signal handler");
            continue;
        }
    }
}


static void setup_sighandlers(void) {
    struct sigaction act;
    int i;

    // Install the termination handlers
    // Note: we're assuming that none of these signal handlers are already trapped.
    // If they are, we'll need to modify this code to save the previous handler and
    // call it after we restore stdin to its previous state.
    memset(&act, '\0', sizeof(act));
    act.sa_handler = &sighandler;
    for (i = 0; quit_signals[i]; i++) {
        if (sigaction(quit_signals[i], &act, NULL) < 0) {
            printf("Error installing signal handler");
            continue;
        }
    }
}

void pump_stdin_async(int outfd) {
    // Put stdin into raw mode
    set_stdin_raw();

    // Pump data from stdin to the PTY
    pump_async(STDIN_FILENO, outfd);
}

static void fork_for_samsung(void)
{
    // Samsung CONFIG_SEC_RESTRICT_SETUID wants the parent process to have
    // EUID 0, or else our setresuid() calls will be denied.  So make sure
    // all such syscalls are executed by a child process.
    int rv;

    switch (fork()) {
        case 0:
            return;
        case -1:
            ERR_EXIT("fork failed\n");
        default:
            if (wait(&rv) < 0) {
                exit(1);
            } else {
                exit(WEXITSTATUS(rv));
            }
    }
}



int client_main(int argc,char *argv[])
{

//    fork_for_samsung();  //可以不用，也可以写这个进程接受client进程退出状态


    int uid = getuid();
    char pwd[256];
    int ptmx;
    char pts_slave[PATH_MAX];
    //socket
    int socketfd = socket(AF_LOCAL, SOCK_STREAM, 0);
    if (socketfd < 0) {
        ERR_EXIT("socket");
        exit(-1);
    }
    if (fcntl(socketfd, F_SETFD, FD_CLOEXEC)) {
        ERR_EXIT("fcntl FD_CLOEXEC");
        exit(-1);
    }
    struct sockaddr_un cliaddr;

    memset(&cliaddr, 0, sizeof(cliaddr));
    cliaddr.sun_family=AF_UNIX;
    strcpy(cliaddr.sun_path+1,REQUESTOR_SOCKET);
    cliaddr.sun_path[0]='\0';


    //客户端不需要绑定和监听
    //connect 用本地套接字连接服务器的地址
    if(connect(socketfd,(struct sockaddr*)&cliaddr,sizeof(cliaddr))<0)
        ERR_EXIT("connect");

    int atty = 0;
    if (isatty(STDIN_FILENO))atty |= ATTY_IN;
    if (isatty(STDOUT_FILENO)) atty |= ATTY_OUT;
    if (isatty(STDERR_FILENO)) atty |= ATTY_ERR;


    if (atty) {
        // We need a PTY. Get one. 终端运行
        ptmx = pts_open(pts_slave, sizeof(pts_slave));
        if (ptmx < 0) {
            ERR_EXIT("pts_open failed\n");
        }
    } else {
        // 不是通过终端调用，没有tty
        pts_slave[0] = '\0';
    }

    //开始进程通讯


    write_int(socketfd, getpid());
    write_int(socketfd, uid);
    write_string(socketfd, getcwd(pwd,256));
    write_int(socketfd, argc-1);
    for(int i=1;i<argc;i++){
        write_string(socketfd, argv[i]);
    }


    write_string(socketfd, pts_slave);

    // Send stdin
    if (atty & ATTY_IN) {
        // Using PTY
        send_fd(socketfd, -1);
    } else {
        send_fd(socketfd, STDIN_FILENO);
    }

    // Send stdout
    if (atty & ATTY_OUT) {
        // Forward SIGWINCH
        watch_sigwinch_async(STDOUT_FILENO, ptmx);

        // Using PTY
        send_fd(socketfd, -1);
    } else {
        send_fd(socketfd, STDOUT_FILENO);
    }


    // Send stderr
    if (atty & ATTY_ERR) {
        // Using PTY
        send_fd(socketfd, -1);
    } else {
        send_fd(socketfd, STDERR_FILENO);
    }


    // Wait for acknowledgement from daemon
    read_int(socketfd);

    //在这之前，都是在和服务器建立连接，获取pts
    //在这之后，处理获取的pts,
    if (atty & ATTY_IN) {
        setup_sighandlers();
        pump_stdin_async(ptmx);
    }
    if (atty & ATTY_OUT) {
        pump_stdout_blocking(ptmx);
    }
    //阻塞防止进程退出，这样通道可以一直进行通讯，
    //当对面执行进程退出的时候，对面
    int code = read_int(socketfd);
    close(socketfd);
    printf("client exited %d\n", code);

    return 0;
}



