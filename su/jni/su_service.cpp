


#include <sys/wait.h>
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

static int daemon_accept_handlr(int clientfd) {

#ifdef  __ANDROID__
    char *bin_exec = "/system/bin/sh";
    char *sh_exec = "sh";
#elif __linux__
    char *bin_exec = "/bin/bash";
    char *sh_exec = "bash";
#endif
        signal(SIGCHLD, SIG_IGN); // 忽略子进程结束信号，防止出现僵尸进程
    int pid = read_int(clientfd);
    printf("remote pid: %d \n", pid);
    daemon_from_uid = read_int(clientfd);
    printf("remote uid: %d \n", daemon_from_uid);
    std::string env_pwd = "PWD=";
    env_pwd = env_pwd + read_string(clientfd);
    printf("remote pwd: %s \n", env_pwd.c_str());
    int argc = read_int(clientfd);
    printf("remote argc: %d \n", argc);
    std::string arg ;
    for(int i=0;i<argc;i++){
        char * remote_args =  read_string(clientfd);
        arg = arg +" "+ remote_args;
        printf("remote arg: %s \n", remote_args);
    }
    char *pts_slave = read_string(clientfd);
    printf("remote pts_slave: %s \n", pts_slave);


    int infd  = recv_fd(clientfd);
    int outfd = recv_fd(clientfd);
    int errfd = recv_fd(clientfd);


    // acknowledgement  to  daemon
    write_int(clientfd, 1);

    int child = fork();
    if (child < 0) {
        // fork failed, send a return code and bail out
        perror( "unable to fork");
        write(clientfd, &child, sizeof(int));
        close(clientfd);
        return child;
    }

    if (child != 0) {
        //为什么要fork一个子进程
        //子进程是真是执行shell，输入输出的进程，而这个父进程，等待这个子进程退出，或者返回结果
        int status, code;
        free(pts_slave);
        printf("waiting for child exit\n");
        printf("current parent pid = %d\n",getpid());
        printf("current chiled pid = %d\n",child);
        if (waitpid(child, &status, 0) > 0) {
            code = WEXITSTATUS(status);
        }
        else {
            code = -1;
        }

        // Pass the return code back to the client
        printf("sending code\n");
        if (write(clientfd, &code, sizeof(int)) != sizeof(int)) {
            printf("unable to write exit code");
        }

        close(clientfd);
        printf("child exited\n");
        return code;
    }
    close (clientfd);

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
        printf("not tty ,ioctl TIOCSCTTY su service\n");
        if (isatty(infd)) {
            ioctl(infd, TIOCSCTTY, 1);
        }
    }
    free(pts_slave);
    //所有的输入输出都重定向到 /dev/pts
    if (-1 == dup2(outfd, STDOUT_FILENO)) {
        ERR_EXIT("dup2 child outfd");
    }
    if (-1 == dup2(errfd, STDERR_FILENO)) {
        ERR_EXIT("dup2 child errfd");
    }
    if (-1 == dup2(infd, STDIN_FILENO)) {
        ERR_EXIT("dup2 child infd");
    }

    close(infd);
    close(outfd);
    close(errfd);
    char* argument_list[4];

    if(arg.size() > 0){
        argument_list[0]= sh_exec;
        argument_list[1]="-c";
        argument_list[2]=(char *)arg.c_str();
        argument_list[3]= NULL;

    } else{
        argument_list[0]= sh_exec;
        argument_list[1]= NULL;
    }
    putenv((char*)env_pwd.c_str());  //设置环境变量 //我这里目前只设置了PWD 环境变量，shell会自动切换到这个目录
    execvp(bin_exec, argument_list);
    printf( "Cannot execute %s: %s\n", bin_exec, strerror(errno));
    free(pts_slave);
}




int service_main(void)
{
    //socket
    int listenfd;

    //listenfd=socket(PF_INET,SOCK_STREAM,0);
    if((listenfd=socket(AF_LOCAL,SOCK_STREAM,0))<0)
    {
        ERR_EXIT("socket");
    }

    if (fcntl(listenfd, F_SETFD, FD_CLOEXEC)) {
        ERR_EXIT("fcntl FD_CLOEXEC");
    }

    //填充地址结构
    struct sockaddr_un servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sun_family=AF_LOCAL;
    strcpy(servaddr.sun_path+1,REQUESTOR_SOCKET);
    servaddr.sun_path[0]='\0';

    //bind 绑定listenfd和本地地址结构
    if(bind(listenfd,(struct sockaddr*)&servaddr,sizeof(servaddr))<0)
    {
        ERR_EXIT("bind");
    }

    if(listen(listenfd,SOMAXCONN)<0)
    {
        ERR_EXIT("listen");
    }


    int client;   //已连接套接字(主动)
    printf("curren pid %d\n",getpid());
    while ((client = accept(listenfd, NULL,NULL)) > 0) {
        if (fork_zero_fucks() == 0) {
            close(listenfd);
            return daemon_accept_handlr(client);
        }
        else {
            close(client);
        }
    }
    //关闭套接口
    close(client);
    close(listenfd);
    return 0;
}


