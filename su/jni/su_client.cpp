

#include <climits>
#include <string>
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





int client_main(int argc,char *argv[])
{
    int uid = getuid();
    char pwd[256];
    int ptmx;
    char pts_slave[PATH_MAX];
    //socket
    int socketfd;
    if((socketfd=socket(AF_UNIX,SOCK_STREAM | SOCK_CLOEXEC, 0))<0)
    {
        ERR_EXIT("socket");
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
        printf("pts_open");
        ptmx = pts_open(pts_slave, sizeof(pts_slave));
        if (ptmx < 0) {
            ERR_EXIT("pts_open");
        }
    } else {
        // 不是通过终端调用，没有tty
        pts_slave[0] = '\0';
    }


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
    //关闭套接字
    int code = read_int(socketfd);
    close(socketfd);
    printf("client exited %d", code);

    return 0;
}