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

#define AUTHOR "I, yijiangw, have read and understood the course academic integrity policy."

#define INF -1

using namespace std;

//连接指定 IP 和 端口  返回 socket fd
int connect_to_host(char *server_ip, int server_port);
//从 fd  返回 IP
string getIP(int sockfd);
in_addr getIP_in_addr(int sockfd);
//用指定端口和类别初始化指定socket
int socket_init(int *targetsocket,int port,int type);
//accept 连接指定socket 并设置到新fd
int socket_accept(int targetsocket,int *new_fd);

struct in_addr hostIP,controllerIP; //本机IP, controller IP   in_addr  就是 uint32_t   that's a 32-bit int (4 bytes)
int cmd_port,data_port,DV_port;  //存储本机的三个端口
int cmd_socket,data_socket,DV_socket; //cmd 监听listen连接socket  数据监听连接socket  距离向量UDP接受socket
int cmd_fd,data_fd[MAX_CLIENT]; //命令接受 fd  数据文件接受fd  距离向量UDP fd 和 DV_socket 一样
int max_fd, fd_index;  // 最大fd  当前fd 用于循环检查
int selret; //select阻塞结束后的执行结果，小于0执行出错 大于0成功
struct timeval tv; //select 间隔时间


uint16_t number_of_routers;
uint16_t periodic_interval;
unsigned short int host_ID;

//struct sockaddr_in cmd_addr, data_addr, client_addr; //存地址信息用的 (取消)
//
struct sockaddr_in  server_addr,client_addr;// 建立socket中使用

fd_set master_list, watch_list;  //master_list存储 fd list   watch_list为 mster list 副本 使用中会变化作为临时存储
//路由ID IP对应表
struct in_addr routerIP[MAX_CLIENT];    //routerIP [n] 是 ID 为 n 的路由IP  || INIT初始化后不变
//路由 ID 端口对应表
unsigned short int routerPort[MAX_CLIENT][2];  //[n][] index n 是相应的 routerID; [n][0] 是 DV更新UDP端口; [n][1] 是 文件传输连接TCP端口; || INIT初始化后不变
//路由表
unsigned short int routing_table[MAX_CLIENT][2]; //[n][] index n 是相应的 routerID; [n][0] 是 nexthop; [n][1] 是 cost; || INIT初始化，DV过程改变
//拓扑图
unsigned short int topology [MAX_CLIENT][MAX_CLIENT]; // [x][y]表示x和y之间的cost， 初始化为-1 0xFFFF || INIT初始化，UPDATE命令改变
//是否相邻路由
bool linked[MAX_CLIENT]; //linked [n] 是 true 的话 ID 为 n 的路由IP 直接与本机相连

//大多数数据包的基本数据结构  controller命令交互和DV更新
struct packet_abstract
{
    char head[MOSTHEAD_SIZE];
    char payload[BUFFER_SIZE-MOSTHEAD_SIZE];
};
struct controlpk_head
{
    struct in_addr destination;
    uint8_t control_code;   //就是 unsigned char
    uint8_t response_time;
    uint16_t payload_len;
};

struct control_reponse_head
{
    struct in_addr controller_ip;
    uint8_t control_code;
    uint8_t response_code;
    uint16_t payload_len;
};


//文件数据包的数据结构
struct data_packet
{
    struct in_addr destination;
    short trans_id;
    short TTL;
    short seq;
    unsigned int FIN;  //初始化时全填0， 然后在finish的时候 设置 FIN = FINSH
    char payload[1024];
};

//DV路由更新包头
struct routingupdate_head
{
    short update_count;
    unsigned short source_port;
    struct in_addr source_ip;
};
typedef routingupdate_head DVpk_head;  //两种名称 随便选用

//DV路由更新路由项
struct routingupdate_router
{
    struct in_addr router_ip;
    unsigned short source_port;
    short padding;
    short router_id;
    unsigned short cost;
};
typedef routingupdate_head DVpk_item;

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
    // get extern ip
    char szHostName[256];
    gethostname(szHostName, sizeof(szHostName));
    struct hostent *hostinfo = gethostbyname(szHostName);
    char * extern_ip = inet_ntoa(*(struct in_addr*)*hostinfo->h_addr_list);
    hostIP = *(struct in_addr*)*hostinfo->h_addr_list;


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
    // memcpy(&watch_list, &master_list, sizeof(master_list));
    // selret = select(max_fd + 1, &watch_list, NULL, NULL, NULL);
    // if (selret < 0)
    //     perror("select failed.");
    // else if(selret > 0&&!socket_accept(cmd_socket,&cmd_fd))
    // {
    //     perror("cmd connect accept failed.");
    //     return 0;
    // }
    // printf("controller connected!\n");
    // FD_SET(cmd_fd, &master_list);

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
                        printf("controller reconnected %d!\n", fd_index);
                        FD_SET(cmd_fd, &master_list);
                        controllerIP = getIP_in_addr(cmd_fd);
                        printf("%s\n", inet_ntoa(controllerIP));
                    }
                    else if(fd_index==cmd_fd)   //controller 命令
                    {
                        //准备buffer 接收header
                        char *buffer = (char *)malloc(sizeof(char) * MOSTHEAD_SIZE);
                        memset(buffer, '\0', MOSTHEAD_SIZE);

                        if (recv(fd_index, buffer, MOSTHEAD_SIZE, 0) <= 0)// controller 断开
                        {
                            close(fd_index);
                            printf("controller terminated connection %d!\n", fd_index);
                            /* Remove from watched list */
                            FD_CLR(fd_index, &master_list);
                        }
                        else
                        {
                            // read control header first
                            struct controlpk_head *cpk_head = (struct controlpk_head *)malloc(sizeof(char) * MOSTHEAD_SIZE);
                            memset(cpk_head, '\0', MOSTHEAD_SIZE);
                            memcpy(cpk_head, buffer, MOSTHEAD_SIZE);
                            int control_payload_len = ntohs(cpk_head->payload_len);
                            int control_code = cpk_head->control_code;
                            // check if need recieve control payload
                            printf("control payload len: %d", control_payload_len);
                            char *control_payload_buffer;
                            if(control_payload_len != 0) {
                                control_payload_buffer = (char *)malloc(sizeof(char) * control_payload_len);
                                memset(control_payload_buffer, '\0', control_payload_len);
                                recv(fd_index, control_payload_buffer, control_payload_len, 0);
                            }
                            // prepare control response
                            struct control_reponse_head *crp_head =(struct control_reponse_head *)malloc(sizeof(char) * MOSTHEAD_SIZE);
                            memset(crp_head, '\0', MOSTHEAD_SIZE);
                            crp_head->control_code = 0;
                            crp_head->controller_ip = controllerIP;
                            crp_head->payload_len = 0;
                            crp_head->response_code = 0;
                            switch(control_code) {
                                // AUTHOR [Control Code: 0x00]
                                case 0:
                                {

                                    char *author_str = (char *)malloc(sizeof(AUTHOR));
                                    strcpy(author_str, AUTHOR);
                                    int author_payload_len = strlen(author_str);
                                    crp_head->control_code = 0;
                                    crp_head->response_code = 0;
                                    int response_code = 0;
                                    crp_head->payload_len = htons(author_payload_len);
                                    int author_response_len = MOSTHEAD_SIZE+author_payload_len;
                                    char *response_buffer = (char *)malloc(sizeof(char) * author_response_len);
                                    memset(response_buffer, '\0', author_response_len);
                                    memcpy(response_buffer, crp_head, MOSTHEAD_SIZE);
                                    memcpy(response_buffer+MOSTHEAD_SIZE, author_str, author_payload_len);
                                    int sentbytes = send(fd_index, response_buffer, author_response_len, 0);
                                    break;
                                }
                                // INIT [Control Code: 0x01]
                                case 1:
                                {
                                    printf("\n*****INIT******\n");
                                    memcpy(&number_of_routers, control_payload_buffer, 2);
                                    memcpy(&periodic_interval, control_payload_buffer+2, 
                                    2);
                                    number_of_routers = ntohs(number_of_routers);
                                    periodic_interval = ntohs(periodic_interval);
                                    /* cost_list[i][0] -> router_id 
                                    *  cost_list[i][1] -> cost of such link
                                    * */
                                    unsigned short int cost_list[MAX_CLIENT][2];
                                    for(int i=0; i < number_of_routers; i++) {
                                        int _offset = 4 + (12 * i);
                                        unsigned short int router_id, port_1, port_2, cost;
                                        struct in_addr router_ip;
                                        memcpy(&router_id, control_payload_buffer + _offset, 2);
                                        router_id = ntohs(router_id);
                                        memcpy(&port_1, control_payload_buffer+_offset+2, 2);
                                        port_1 = ntohs(port_1);
                                        memcpy(&port_2, control_payload_buffer+_offset+4, 2);
                                        port_2 = ntohs(port_2);
                                        memcpy(&cost, control_payload_buffer+_offset+6, 2);
                                        cost = ntohs(cost);
                                        memcpy(&router_ip, control_payload_buffer+_offset+8, 4);
                                        printf("ID: %d, PORT1: %d, PORT2: %d IP:%s COST:%d\n", router_id, port_1, port_2, inet_ntoa(router_ip), cost);
                                        in_addr a;
                                        routerIP[router_id] = router_ip;
                                        routerPort[router_id][0] = port_1;
                                        routerPort[router_id][1] = port_2;
                                        cost_list[i][0] = router_id;
                                        cost_list[i][1] = cost;
                                        if (cost == 0) {
                                            host_ID = router_id;
                                            printf("MY ID IS %d\n", host_ID);
                                        }
                                    }
                                    // Update the DV routing table
                                    for(int i=0; i < number_of_routers; i++) {
                                        /*
                                        * [n][] index n 是相应的 routerID; 
                                        * [n][0] 是 nexthop; [n][1] 是 cost; 
                                        * INIT初始化，DV过程改变 
                                        */
                                        routing_table[host_ID][0] = cost_list[i][0];
                                        routing_table[host_ID][1] = cost_list[i][1];
                                    }
                                    // Send response (ONLY HEADER)
                                    char *response_buffer = (char *)malloc(sizeof(char) * MOSTHEAD_SIZE);
                                    memset(response_buffer, '\0', MOSTHEAD_SIZE);
                                    crp_head->control_code = 1;
                                    memcpy(response_buffer, crp_head, MOSTHEAD_SIZE);
                                    send(fd_index, response_buffer, MOSTHEAD_SIZE, 0);
                                    break;
                                }

                                
                            }
                            

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

in_addr getIP_in_addr(int sockfd)
{

    struct sockaddr_in sa;
    unsigned int len = sizeof(sa);
    getpeername(sockfd, (struct sockaddr *)&sa, &len);


    return (sa.sin_addr);
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