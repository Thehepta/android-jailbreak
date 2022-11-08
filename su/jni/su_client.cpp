

#include <climits>
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





int client_main()
{
    char msg[PATH_MAX];
    int uid = getuid();
    char pwd[256];
    int ptmx;
    char pts_slave[PATH_MAX];
    //socket
    int socketfd;
    if((socketfd=socket(PF_INET,SOCK_STREAM,IPPROTO_TCP))<0)
    {
        ERR_EXIT("socket");
    }

    struct sockaddr_in cliaddr;
    memset(&cliaddr, 0, sizeof(cliaddr));
    cliaddr.sin_family = AF_INET;
    cliaddr.sin_port = htons(0);
    cliaddr.sin_addr.s_addr = htonl(INADDR_ANY); //htonl可以省略，因为INADDR_ANY是全0的




    if(bind(socketfd,(struct sockaddr*)&cliaddr,sizeof(cliaddr))<0)
    {
        ERR_EXIT("bind");
    }

    //指定服务器的地址结构
    struct sockaddr_in servaddr;
    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family=AF_INET;
    servaddr.sin_port=htons(5188);
    servaddr.sin_addr.s_addr=inet_addr("127.0.0.1");

    //客户端不需要绑定和监听
    //connect 用本地套接字连接服务器的地址
    if(connect(socketfd,(struct sockaddr*)&servaddr,sizeof(servaddr))<0)
        ERR_EXIT("connect");

    int atty = 0;
    if (isatty(STDIN_FILENO))  atty |= ATTY_IN;
    if (isatty(STDOUT_FILENO)) atty |= ATTY_OUT;
    if (isatty(STDERR_FILENO)) atty |= ATTY_ERR;

    if (atty) {
        // We need a PTY. Get one.
        ptmx = pts_open(pts_slave, sizeof(pts_slave));
        if (ptmx < 0) {
            ERR_EXIT("pts_open");
        }
    } else {
        ERR_EXIT("atty is null");
    }
    write_int(socketfd, getpid());
    write_string(socketfd, pts_slave);
    write_int(socketfd, uid);
    write_string(socketfd, getcwd(pwd,256));

    // Forward SIGWINCH
    watch_sigwinch_async(STDOUT_FILENO, ptmx);

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
    close(socketfd);

    return 0;
}