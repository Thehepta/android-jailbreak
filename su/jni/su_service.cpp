


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
    daemon_from_uid = read_int(fd);
    printf("remote uid: %d \n", daemon_from_uid);
    std::string env_pwd = "PWD=";
    env_pwd = env_pwd + read_string(fd);
    printf("remote pwd: %s \n", env_pwd.c_str());
    int argc = read_int(fd);
    printf("remote argc: %d \n", argc);
    std::string arg ;
    for(int i=0;i<argc;i++){
        char * remote_args =  read_string(fd);
        arg = arg +" "+ remote_args;
        printf("arg: %s \n", remote_args);
    }
    printf("arg: %s  argc: %d\n", arg.c_str(),arg.size());


    char *pts_slave = read_string(fd);
    printf("remote pts_slave: %s \n", pts_slave);


    int infd  = recv_fd(fd);
    int outfd = recv_fd(fd);
    int errfd = recv_fd(fd);


    // acknowledgement  to  daemon
    write_int(fd, 1);


    int child = fork();
    if (child < 0) {
        // fork failed, send a return code and bail out
        perror( "unable to fork");
        write(fd, &child, sizeof(int));
        close(fd);
        return child;
    }

    if (child != 0) {
        int status, code;

        free(pts_slave);

        printf("waiting for child exit");
        if (waitpid(child, &status, 0) > 0) {
            code = WEXITSTATUS(status);
        }
        else {
            code = -1;
        }

        // Pass the return code back to the client
        printf("sending code");
        if (write(fd, &code, sizeof(int)) != sizeof(int)) {
            printf("unable to write exit code");
        }

        close(fd);
        printf("child exited");
        return code;
    }
    close (fd);

    //分离终端，将当前进程设置成当前进程组的控制终端，终端是以进程组为单位的
    if (setsid() == (pid_t) -1) {
        ERR_EXIT("setsid failed");
    }
    // Opening the TTY has to occur after the
    // fork() and setsid() so that it becomes
    // our controlling TTY and not the daemon's
    int ptsfd = 0;

    if (pts_slave[0]) {
        printf("open TIOCSCTTY");
        ptsfd = open(pts_slave, O_RDWR);
        if (ptsfd == -1) {
            ERR_EXIT("open(pts_slave) daemon");
        }
        if (infd < 0)  {
            printf("daemon: stdin using PTY");
            infd  = ptsfd;
        }
        if (outfd < 0) {
            printf("daemon: stdout using PTY");
            outfd = ptsfd;
        }
        if (errfd < 0) {
            printf("daemon: stderr using PTY");
            errfd = ptsfd;
        }
    }else{
        //调用对象不是终端设备
        printf("ioctl TIOCSCTTY su service\n");
        if (isatty(infd)) {
            ioctl(infd, TIOCSCTTY, 1);
        }
    }
    free(pts_slave);
    //所有的输入输出都重定向到 /dev/pts
    if (-1 == dup2(outfd, STDOUT_FILENO)) {
        ERR_EXIT("dup2 child outfd");
    }
    if (-1 == dup2(infd, STDERR_FILENO)) {
        ERR_EXIT("dup2 child errfd");
    }
    if (-1 == dup2(errfd, STDIN_FILENO)) {
        ERR_EXIT("dup2 child infd");
    }

    close(infd);
    close(outfd);
    close(errfd);
    char* argument_list[4];

    if(arg.size() > 0){
        argument_list[0]="bash";
        argument_list[1]="-c";
        argument_list[2]=(char *)arg.c_str();
        argument_list[3]= NULL;

    } else{
        argument_list[0]="bash";
        argument_list[1]= NULL;
    }
    putenv((char*)env_pwd.c_str());  //设置环境变量 //我这里目前只设置了PWD 环境变量，shell会自动切换到这个目录
    execvp(sh_exec, argument_list);
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


