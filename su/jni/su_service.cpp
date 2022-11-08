


#include <sys/wait.h>
#include <sys/stat.h>
#include <string>
#include "su_service.h"
#include "pts.h"

int is_daemon = 0;
int daemon_from_uid = 0;
int daemon_from_pid = 0;

int fork_zero_fucks() {
    int pid = fork();
    if (pid) {
        int status;
        waitpid(pid, &status, 0);
        return pid;
    }
    else {
        if (pid = fork())
            exit(0);
        return 0;
    }
}


static void cleanup_signal(int sig) {
    printf("cleanup_signal %d",sig);
    exit(128 + sig);
}

static int daemon_accept(int fd) {

#ifdef  __ANDROID__
    char *bin_exec = "/system/bin/sh";
    char *sh_exec = "sh";
#elif __linux__
    char *bin_exec = "/bin/bash";
    char *sh_exec = "bash";
#endif
    signal(SIGCHLD, SIG_IGN); // 忽略子进程结束信号，防止出现僵尸进程
    printf("daemon_accept pid %d \n",getpid());
    int pid = read_int(fd);
    printf("remote pid: %d \n", pid);
    char *pts_slave = read_string(fd);
    printf("remote pts_slave: %s \n", pts_slave);
    daemon_from_uid = read_int(fd);
    printf("remote uid: %d \n", daemon_from_uid);
    std::string env_pwd = "PWD=";
    env_pwd = env_pwd + read_string(fd);
    printf("remote pwd: %s \n", env_pwd.c_str());

    write_int(fd, 1);

    //分离终端，将当前进程设置成当前进程组的控制终端，终端是以进程组为单位的
    if (setsid() == (pid_t) -1) {
        ERR_EXIT("setsid failed");
    }
    // Opening the TTY has to occur after the
    // fork() and setsid() so that it becomes
    // our controlling TTY and not the daemon's
    int ptsfd = 0;
    ptsfd = open(pts_slave, O_RDWR);
    if (ptsfd == -1) {
        ERR_EXIT("open(pts_slave) daemon");
    }

    //所有的输入输出都重定向到 /dev/pts
    if (-1 == dup2(ptsfd, STDOUT_FILENO)) {
        ERR_EXIT("dup2 child outfd");
    }
    if (-1 == dup2(ptsfd, STDERR_FILENO)) {
        ERR_EXIT("dup2 child errfd");
    }
    if (-1 == dup2(ptsfd, STDIN_FILENO)) {
        ERR_EXIT("dup2 child infd");
    }

    putenv((char*)env_pwd.c_str());  //设置环境变量 //我这里目前只设置了PWD 环境变量，shell会自动切换到这个目录
    execlp(bin_exec,sh_exec, nullptr);
    fprintf(stderr, "Cannot execute %s: %s\n", bin_exec, strerror(errno));
    free(pts_slave);
}




int service_main(void)
{
    //socket
    int listenfd;

    //listenfd=socket(PF_INET,SOCK_STREAM,0);
    if((listenfd=socket(PF_INET,SOCK_STREAM,IPPROTO_TCP))<0)
    {
        ERR_EXIT("socket");
    }

    //填充地址结构
    struct sockaddr_in servaddr;
    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family=AF_INET;
    servaddr.sin_port=htons(5188);
    servaddr.sin_addr.s_addr=htonl(INADDR_ANY); //htonl可以省略，因为INADDR_ANY是全0的
    //servaddr.sin_addr.s_addr=inet_addr("127.0.0.1");
    //inet_aton("127.0.0.1",&servaddr.sin_addr);




    //地址复用
    int on=1;
    if(setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on))<0)
    {
        ERR_EXIT("setsocketopt");
    }

    //bind 绑定listenfd和本地地址结构
    if(bind(listenfd,(struct sockaddr*)&servaddr,sizeof(servaddr))<0)
    {
        ERR_EXIT("bind");
    }

    if(listen(listenfd,SOMAXCONN)<0)
    {
        ERR_EXIT("listen");
    }


    // 调用listen函数后，就成了被动套接字，否则是主动套接字
    // 主动套接字：发送连接(connect)
    // 被动套接字：接收连接(accept)
    //对方的地址
    struct sockaddr_in peeraddr;
    socklen_t peerlen=sizeof(peeraddr);
    int conn;   //已连接套接字(主动)
    printf("pid %d",getpid());
    while ((conn = accept(listenfd, (struct sockaddr*)&peeraddr,&peerlen)) > 0) {
        printf("client: ip=%s | port=%d\n",inet_ntoa(peeraddr.sin_addr),ntohs(peeraddr.sin_port));
        if (fork_zero_fucks() == 0) {
            close(listenfd);
            return daemon_accept(conn);
        }
        else {
            close(conn);
        }
    }
    //关闭套接口
    close(conn);
    close(listenfd);
    return 0;
}


