//
// Created by chic on 2022/8/22.
//

#ifndef SUPERSU_CLIENT_H
#define SUPERSU_CLIENT_H
#include <cstdio>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <termios.h>


int client_main(int argc,char *argv[]);

#endif //SUPERSU_CLIENT_H
