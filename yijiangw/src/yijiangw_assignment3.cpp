/**
 * @yijiangw_assignment3
 * @author  YijiangWang <yijiangw@buffalo.edu>
 * @version 1.0
 *
 * @section LICENSE
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details at
 * http://www.gnu.org/copyleft/gpl.html
 *
 * @section DESCRIPTION
 *
 * This contains the main function. Add further description here....
 */

/**
 * main function
 *
 * @param  argc Number of arguments
 * @param  argv The argument list
 * @return 0 EXIT_SUCCESS
 */
#include <stdio.h>
#include <cstdlib>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string>
#include <iostream>
#include <list>
//最大用户数
#define MAX_CLIENT 20
//connect 用户缓存数目
#define BACKLOG MAX_CLIENT
//距离向量UDP fd 和 DV_socket 一样 直接就可以接受不用accept新fd
#define DV_fd DV_socket
#define TCP_TYPE SOCK_STREAM
#define UDP_TYPE SOCK_DGRAM

#define FINISH 0x80000000

//数据包缓存4k
#define BUFFER_SIZE 4096
#define MOSTHEAD_SIZE 8
#define DATAHEAD_SIZE 12

#define TRUE 1
#define TIMEOUT 1

using namespace std;

//连接指定 IP 和 端口  返回 socket fd
int connect_to_host(char *server_ip, int server_port);
//从 fd  返回 IP
string getIP(int sockfd);
//用指定端口和类别初始化指定socket
int socket_init(int *targetsocket,int port,int type);
//accept 连接指定socket 并设置到新fd
int socket_accept(int targetsocket,int *new_fd);

int cmd_port,data_port,DV_port;
int cmd_socket,data_socket,DV_socket; //cmd 监听listen连接socket  数据监听连接socket  距离向量UDP接受socket
int cmd_fd,data_fd[MAX_CLIENT]; //命令接受 fd  数据文件接受fd  距离向量UDP fd 和 DV_socket 一样
int max_fd, fd_index;  // 最大fd  当前fd 用于循环检查
int selret; //select阻塞结束后的执行结果，小于0执行出错 大于0成功
struct timeval tv; //select 间隔时间

//struct sockaddr_in cmd_addr, data_addr, client_addr; //存地址信息用的
struct sockaddr_in  server_addr,client_addr;

fd_set master_list, watch_list;  //master_list存储 fd list   watch_list为 mster list 副本 使用中会变化作为临时存储


//所有数据包的数据结构
struct packet_abstract
{
    char head[MOSTHEAD_SIZE];
    char payload[BUFFER_SIZE-MOSTHEAD_SIZE];
};
struct data_packet
{
    struct in_addr destination;
    short trans_id;
    short TTL;
    short seq;
    unsigned int FIN;  //初始化时全填0， 然后在finish的时候 设置 FIN = FINSH
    char payload[1024];
};
struct routingupdate_head
{
    short update_count;
    unsigned short source_port;
    struct in_addr source_ip;
};
struct routingupdate_router
{
    struct in_addr router_ip;
    unsigned short source_port;
    short padding;
    short router_id;
    unsigned short cost;
};


int main(int argc, char **argv)
{
    /*Start Here*/
    if (argc != 2)
    {
        printf("Usage:%s [port]\n", argv[0]);
        exit(-1);
    }
    struct timeval settv;


    //begin
    cmd_port = atoi(argv[1]);
    tv.tv_sec = TIMEOUT;
    tv.tv_usec = 0;

    //初始化 cmd socket 并开始监听
    socket_init(&cmd_socket,cmd_port,TCP_TYPE);

    /* Zero select FD sets */
    FD_ZERO(&master_list);
    FD_ZERO(&watch_list);

    /* Register the cmd listen socket */
    FD_SET(cmd_socket, &master_list);

    max_fd = cmd_socket;
    //wait be connected by controller
    memcpy(&watch_list, &master_list, sizeof(master_list));
    selret = select(max_fd + 1, &watch_list, NULL, NULL, NULL);
    if (selret < 0)
        perror("select failed.");
    else if(selret > 0&&!socket_accept(cmd_socket,&cmd_fd))
    {
        perror("cmd connect accept failed.");
        return 0;
    }
    printf("controller connected!\n");
    FD_SET(cmd_fd, &master_list);

    //开始接受数据
    while(1)
    {
        memcpy(&watch_list, &master_list, sizeof(master_list));
        settv = tv;
        selret = select(max_fd + 1, &watch_list, NULL, NULL, &settv);  //利用 远小于DV更新间隔的时间段tv 作为select 时间段
        if(selret > 0) //有 fd 活动
        {

            for (fd_index = 0; fd_index <= max_fd; fd_index++)
            {

                if (FD_ISSET(fd_index, &watch_list)) //索引FD 有活动
                {
                    if (fd_index==cmd_socket)  //controller 重连
                    {
                        socket_accept(cmd_socket,&cmd_fd);
                        printf("controller reconnected!\n");
                        FD_SET(cmd_fd, &master_list);
                    }
                    else if(fd_index==cmd_fd)   //controller 命令
                    {
                        //准备buffer 接收数据
                        char *buffer = (char *)malloc(sizeof(char) * BUFFER_SIZE);
                        memset(buffer, '\0', BUFFER_SIZE);

                        if (recv(fd_index, buffer, BUFFER_SIZE, 0) <= 0)// controller 断开
                            {
                                close(fd_index);
                                printf("controller terminated connection!\n");
                                /* Remove from watched list */
                                FD_CLR(fd_index, &master_list);
                            }
                        else
                        {
                            //获取
                        }
                    }
                    else if(fd_index==data_socket) //数据文件传输 tcp 连接 accept
                    {
                    }
                    else if(fd_index==DV_socket)  // 接收 DV
                    {
                    }
                    else  // 数据 tcp 接受文件数据包
                    {
                    }
                }
            }

        }
        if (selret == 0)// timeout!
        {
            //更新收到直接连接路由表中的下一DV更新包等待时间 减去 tv
            //更新下一次发送更新包剩余时间，如小于等于0 则开始循环向其他相邻路由发送DV
        }
        if (selret == -1)// error
        {
            return -1;
        }

    }

    //创建socket TCP SOCK_STREAM   UDP SOCK_DGRAM
//    cmd_socket = socket(AF_INET, SOCK_STREAM, 0);
//    data_socket = socket(AF_INET, SOCK_STREAM, 0);
//    DV_socket = socket(AF_INET, SOCK_DGRAM, 0);
//
//
//
//    bzero(&server_addr, sizeof(server_addr));
//
//    server_addr.sin_family = AF_INET;  //ipv4
//    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // host to network long
//    server_addr.sin_port = htons(cmd_port);    //host to network short
//
//
//	/* Bind */
//    if (bind(cmd_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
//        perror("Bind failed");
//
//    /* Listen */
//    if (listen(cmd_socket, BACKLOG) < 0)
//        perror("Unable to listen on port");


    return 0;
}

string getIP(int sockfd)
{

    struct sockaddr_in sa;
    unsigned int len = sizeof(sa);
    getpeername(sockfd, (struct sockaddr *)&sa, &len);


    char * extern_ip = inet_ntoa(sa.sin_addr);
    std::cout<<extern_ip<<std::endl;
    return string(extern_ip);
}


int connect_to_host(char *server_ip, int server_port)
{
    int fdsocket, len;
    struct sockaddr_in remote_server_addr;

    fdsocket = socket(AF_INET, SOCK_STREAM, 0);
    if(fdsocket < 0)
        perror("Failed to create socket");

    bzero(&remote_server_addr, sizeof(remote_server_addr));
    remote_server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, server_ip, &remote_server_addr.sin_addr);
    remote_server_addr.sin_port = htons(server_port);

    if(connect(fdsocket, (struct sockaddr*)&remote_server_addr, sizeof(remote_server_addr)) < 0)
    {
        return -1;
        perror("Connect failed");
    }
    return fdsocket;
}

int socket_init(int *targetsocket,int port,int type)
{
    //创建socket TCP SOCK_STREAM   UDP SOCK_DGRAM
    struct sockaddr_in server_addr;
    *targetsocket = socket(AF_INET, type, 0);

    bzero(&server_addr, sizeof(server_addr));

    server_addr.sin_family = AF_INET;  //ipv4
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // host to network long
    server_addr.sin_port = htons(port);    //host to network short


    /* Bind */
    if (bind(*targetsocket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        return 0;
    }
    /* Listen */
    if(type == TCP_TYPE)
        if (listen(*targetsocket, BACKLOG) < 0)
        {
            perror("Unable to listen on port");
            return 0;
        }
    return 1;
}

int socket_accept(int targetsocket,int *new_fd)
{
    struct sockaddr_in client_addr;
    int fdaccept = 0; //新accept的 fd

    socklen_t caddr_len;
    caddr_len = sizeof(client_addr);
    fdaccept = accept(targetsocket, (struct sockaddr *)&client_addr, &caddr_len);
    if (fdaccept < 0)
    {
        perror("Accept failed.\n");
        return 0;
    }

    printf("Accept success\n");

    /* Add to watched socket list */
    FD_SET(fdaccept, &master_list);
    if (fdaccept > max_fd)
        max_fd = fdaccept;
    *new_fd = fdaccept;
    return 1;

}
