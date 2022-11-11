#include <string.h>
#include <sys/wait.h>
#include "su_service.h"
#include "su_client.h"
#include "tool.h"

//小功能测试demo
void java_exec(){

    int child = fork();
    if (child != 0) {

        int status, code;
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
        printf("child exited\n");
        return ;
    }

    if (setsid() == (pid_t) -1) {
        perror("setsid failed");
    }

    char *bin_exec = "/system/bin/sh";
    char* argument_list[4];
    argument_list[0]="sh";
    argument_list[1]= NULL;

    execvp(bin_exec, argument_list);


}

int main(int argc, char *argv[]) {

    if (argc == 2 && strcmp(argv[1], "--daemon") == 0) {
        return service_main();
    }
    return client_main(argc, argv);

//    java_exec();
}

