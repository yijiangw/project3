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
#include <ctime>
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

#define INF 65535

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
unsigned short int cmd_port,data_port,DV_port;  //存储本机的三个端口
int cmd_socket,data_socket,DV_socket; //cmd 监听listen连接socket  数据监听连接socket  距离向量UDP接受socket
int cmd_fd,data_fd[MAX_CLIENT]; //命令接受 fd  数据文件接受fd  距离向量UDP fd 和 DV_socket 一样
int max_fd, fd_index;  // 最大fd  当前fd 用于循环检查
int selret; //select阻塞结束后的执行结果，小于0执行出错 大于0成功
struct timeval tv; //select 间隔时间


uint16_t number_of_routers;
uint16_t periodic_interval;
unsigned short int host_ID;
/*
*   time_t is a long type(32bit)
*   last_start records the last start time of timer
*/
time_t last_start;

void recompute_routing_table_from_topology(int router_id, int max_routing_id);
void print_topology(int max_id);
void print_routing_table(int max_id);
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
int lost_time[MAX_CLIENT]; //对网络中的路由器记录连续没有收到DV包的周期数，当为3时，证明连续3个周期没有收到DV包，将此router视为离线 || INIT初始化，TIMEOUT更新
bool DV_in_last_period[MAX_CLIENT]; //如果在上个周期中收到DV包则true，TIMEOUT时更新lost_time，并将自己重置为全false供下一个周期使用 || INIT初始化，TIMEOUT&DV阶段更新

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
    
    int cnt = 0;
    while(1)
    {   
        if(periodic_interval > 0 && last_start >0 && (time(0) - last_start) >= (periodic_interval)) {
            printf("TIMEOUT %d\n", cnt);
            cnt++;
            last_start = time(0);
            // make UDP DV packet
            char *DV_buffer =  (char *)malloc(BUFFER_SIZE);
            bzero(DV_buffer, BUFFER_SIZE);
            unsigned short int number_of_update_fields = htons(number_of_routers);
            unsigned short int source_router_port = htons(DV_port);
            memcpy(DV_buffer, &number_of_update_fields, 2);
            memcpy(DV_buffer+2, &source_router_port, 2);
            memcpy(DV_buffer+4, &hostIP, 4);
            printf("SOURCE IP: %s\n", inet_ntoa(hostIP));
            unsigned short int padding = htons(0);
            for(int i=1; i <= number_of_routers; i++) {
                int _offset = (i - 1) * 12 + 8;
                unsigned short r_id, r_IP, r_port, r_cost;
                r_id = htons(i);
                r_IP = routerIP[i].s_addr;
                r_port = htons(routerPort[i][0]);
                r_cost = htons(routing_table[i][1]);
                memcpy(DV_buffer+_offset, &r_IP, 4);
                memcpy(DV_buffer+_offset+4, &r_port, 2);
                memcpy(DV_buffer+_offset+6, &padding, 2);
                memcpy(DV_buffer+_offset+8, &r_id, 2);
                memcpy(DV_buffer+_offset+10, &r_cost, 2);
            }
            // send to all routers excluded itself
            for(int i=1; i <= number_of_routers; i++) {
                if(i != host_ID) {
                    struct sockaddr_in addr_to;
                    addr_to.sin_family=AF_INET;
                    addr_to.sin_port=htons(routerPort[i][0]);
                    addr_to.sin_addr.s_addr = routerIP[i].s_addr;
                    int sentlen =sendto(DV_fd,DV_buffer,BUFFER_SIZE,0,(struct sockaddr*)&addr_to,sizeof(addr_to));
                    printf("[ROUTING UPDATE <TO>]ID: %d SENT: %d bytes\n", i, sentlen);
                }
            }
        }
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
                            int control_response_time = cpk_head->response_time;
                            int control_code = cpk_head->control_code;
                            in_addr control_dest_ip = cpk_head->destination;
                            // check if need recieve control payload
                            printf("\nCONTROL HEADER\n");
                            printf("DEST IP: %s\tCONTROL CODE: %d\tRESPONSE TIME: %d\t PAYLOAD LEN:%d\n",inet_ntoa(control_dest_ip), control_code,  control_response_time, control_payload_len);
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
                            crp_head->payload_len = htons(0);
                            crp_head->response_code = 0;
                            switch(control_code) {
                                // AUTHOR [Control Code: 0x00]
                                case 0:
                                {
                                    printf("\n*****AUTHOR******\n");
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
                                    number_of_routers = ntohs(number_of_routers);
                                    memcpy(&periodic_interval, control_payload_buffer+2, 
                                    2);
                                    periodic_interval = ntohs(periodic_interval);
                                    printf("ROUTER NUM: %d ITERVAL: %d\n", number_of_routers, periodic_interval);
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
                                        printf("ID: %d, ROUTER PORT<UDP>: %d, DATA PORT<TCP>: %d IP:%s COST:%d\n", router_id, port_1, port_2, inet_ntoa(router_ip), cost);
                                        in_addr a;
                                        routerIP[router_id] = router_ip;
                                        routerPort[router_id][0] = port_1;
                                        routerPort[router_id][1] = port_2;
                                        cost_list[i][0] = router_id;
                                        cost_list[i][1] = cost;
                                        if (cost == 0) {
                                            host_ID = router_id;
                                            DV_port = port_1;
                                            data_port = port_2;
                                            hostIP = router_ip;
                                            printf("-------------------------------------------------\n");
                                            printf("[SET]\tMY ID IS %d UDP DV PORT: %d TCP DATA PORT: %d\n", host_ID, DV_port, data_port);
                                            if(socket_init(&DV_socket, DV_port, UDP_TYPE)){
                                                FD_SET(DV_socket, &master_list);
                                                if(DV_socket > max_fd) {
                                                    max_fd = DV_socket;
                                                }
                                                printf("STARTED LISTENING DV/ROUTER PORT <UDP>\n");
                                            }
                                            if(socket_init(&data_socket, data_port, TCP_TYPE)){
                                                FD_SET(data_socket, &master_list);
                                                if(data_socket > max_fd) {
                                                    max_fd = data_socket;
                                                }
                                                printf("STARTED LISTENING DATA PORT <TCP>\n");
                                            }
                                            printf("-------------------------------------------------\n");

                                        }
                                    }
                                    // Update the DV routing table
                                    for(int i=0; i < number_of_routers; i++) {
                                        /*
                                        * [n][] index n 是相应的 routerID; 
                                        * [n][0] 是 nexthop; [n][1] 是 cost; 
                                        * INIT初始化，DV过程改变 
                                        */
                                        routing_table[cost_list[i][0]][0] = cost_list[i][0];
                                        routing_table[cost_list[i][0]][1] = cost_list[i][1];
                                    }
                                    printf("\nINITIALIZED ROUTING TABLE\n");
                                    print_routing_table(number_of_routers);

                                    // Init linked & DV
                                    for(int i=1; i <= number_of_routers; i++) {
                                        lost_time[i] = 0;
                                        DV_in_last_period[i] = false;
                                    }
                                    printf("\nINITIALIZED LINKED with all false\n");
                                    /* 
                                    *   Update the topology 
                                    *   Cost from i to j
                                    **/ 
                                    for(int i=1; i <= number_of_routers; i++) {
                                        for(int j=1; j<= number_of_routers; j++) {
                                            if(i == host_ID) {
                                                topology[i][j] = routing_table[j][1];
                                            } else {
                                                topology[i][j] = INF;
                                            }
                                        }
                                    }
                                    printf("\nINITIALIZED TOPOLOGY\n");
                                    print_topology(number_of_routers);
                                    // Send response (ONLY HEADER)
                                    char *response_buffer = (char *)malloc(sizeof(char) * MOSTHEAD_SIZE);
                                    memset(response_buffer, '\0', MOSTHEAD_SIZE);
                                    crp_head->control_code = 1;
                                    memcpy(response_buffer, crp_head, MOSTHEAD_SIZE);
                                    send(fd_index, response_buffer, MOSTHEAD_SIZE, 0);
                                    printf("\nSENT RESPONSE\n");
                                    last_start = time(0);
                                    printf("\nTIMER STARTED\n");
                                    break;
                                }
                                case 2:
                                {
                                    printf("\n*****ROUTING-TABLE******\n");
                                    int response_payload_len = number_of_routers * 8;
                                    int response_buffer_len = MOSTHEAD_SIZE + response_payload_len;
                                    char *response_buffer = (char *)malloc(sizeof(char) * response_buffer_len);
                                    memset(response_buffer, '\0', response_buffer_len);
                                    crp_head->control_code = 2;
                                    crp_head->payload_len = htons(response_payload_len);
                                    memcpy(response_buffer, crp_head, MOSTHEAD_SIZE);
                                    short padding = htons(0);
                                    for(int i=1; i<=number_of_routers; i++) {
                                        int _offset = (i - 1) * 8 + MOSTHEAD_SIZE;
                                        unsigned short int i_id = htons(i);
                                        unsigned short int i_next_hop = htons(routing_table[i][0]);
                                        unsigned short int i_cost = htons(routing_table[i][1]);
                                        memcpy(response_buffer+_offset, &i_id, 2);
                                        memcpy(response_buffer+_offset+2, &padding, 2);
                                        memcpy(response_buffer+_offset+4, &i_next_hop, 2);
                                        memcpy(response_buffer+_offset+6, &i_cost, 2);
                                    }
                                    send(fd_index, response_buffer, response_buffer_len, 0);
                                    printf("\nSENT RESPONSE\n");
                                    break;
                                }
                                case 3:
                                {
                                    printf("\n*****UPDATE******\n");
                                    printf("-----BEFORE UPDATE-----\n");
                                    print_routing_table(number_of_routers);
                                    print_topology(number_of_routers);
                                    unsigned short int r_id, r_new_cost;
                                    memcpy(&r_id, control_payload_buffer, 2);
                                    memcpy(&r_new_cost, control_payload_buffer+2, 2);
                                    r_id = ntohs(r_id);
                                    r_new_cost = ntohs(r_new_cost);
                                    printf("[RECEIVED] -> %d\t COST: %d\n", r_id, r_new_cost);
                                    topology[host_ID][r_id] = r_new_cost;
                                    /* If new cost less than the record in routing_table,
                                    ** update routing_table[host_ID][0] -> this router
                                    **        routing_table[host_ID][1] -> this new cost
                                    */
                                   if(routing_table[r_id][1] > r_new_cost) {
                                       routing_table[r_id][0] = r_id;
                                       routing_table[r_id][1] = r_new_cost;
                                   }
                                    printf("-----AFTER UPDATE-----\n");
                                    print_routing_table(number_of_routers);
                                    print_topology(number_of_routers);
                                    char *response_buffer = (char *)malloc(sizeof(char) * MOSTHEAD_SIZE);
                                    memset(response_buffer, '\0', MOSTHEAD_SIZE);
                                    crp_head->control_code = 3;
                                    memcpy(response_buffer, crp_head, MOSTHEAD_SIZE);
                                    send(fd_index, response_buffer, MOSTHEAD_SIZE, 0);
                                    printf("\nSENT RESPONSE\n");
                                    break;
                                }
                                case 4:
                                {
                                    printf("\n*****CRASH******\n");
                                    char *response_buffer = (char *)malloc(sizeof(char) * MOSTHEAD_SIZE);
                                    memset(response_buffer, '\0', MOSTHEAD_SIZE);
                                    crp_head->control_code = 4;
                                    memcpy(response_buffer, crp_head, MOSTHEAD_SIZE);
                                    send(fd_index, response_buffer, MOSTHEAD_SIZE, 0);
                                    printf("\nSENT RESPONSE\n");
                                    printf("Bye!\n");
                                    exit(0);
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
                        struct sockaddr_in remaddr;     /* remote address */
                        socklen_t addrlen = sizeof(remaddr);   /* length of addresses */
                        char *DV_buffer =  (char *)malloc(BUFFER_SIZE);
                        bzero(DV_buffer, BUFFER_SIZE);
                        int recvlen = recvfrom(fd_index, DV_buffer, BUFFER_SIZE, 0, (struct sockaddr *)&remaddr, &addrlen);
                        // find remote router ID
                        int source_id;
                        for(int i=1; i<=number_of_routers; i++) {
                            if(routerIP[i].s_addr == remaddr.sin_addr.s_addr) {
                                source_id = i;
                                break;
                            }
                        }
                        printf("\n[ROUTING UPDATE <FROM>]ID: %d GOT: %d bytes\n",source_id, recvlen);
                        unsigned short int number_of_update_fields, source_router_port;
                        struct in_addr source_IP;
                        memcpy(&number_of_update_fields, DV_buffer, 2);
                        memcpy(&source_router_port, DV_buffer+2, 2);
                        memcpy(&source_IP, DV_buffer+4, 4);
                        number_of_update_fields = ntohs(number_of_update_fields);
                        source_router_port = ntohs(source_router_port);
                        printf("FIELDS#: %d \tSOURCE PORT: %d \tSOURCE ID: %d \tSOURCE IP: %s\n", number_of_update_fields, source_router_port, source_id, inet_ntoa(source_IP));
                        for(int i=0; i<number_of_update_fields; i++) {
                            unsigned short int dest_id, dest_cost;
                            int _offset = i*12 + 8;
                            memcpy(&dest_id, DV_buffer+_offset+8, 2);
                            memcpy(&dest_cost, DV_buffer+_offset+10, 2);
                            dest_id = ntohs(dest_id);
                            printf("DEST ID: %d\n", dest_id);
                            dest_cost = ntohs(dest_cost);
                            printf("DEST cost: %d\n", dest_cost);
                            topology[source_id][dest_id] = dest_cost;
                        }
                        recompute_routing_table_from_topology(host_ID, number_of_routers);
                        printf("---RECOMPUTED MATRIX---\n");
                        print_routing_table(number_of_routers);
                        print_topology(number_of_routers);
                        printf("\n");
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

void recompute_routing_table_from_topology(int router_id, int max_routing_id) {
    for(int i=1; i<=max_routing_id; i++) {
        if(i != router_id) {
            int orginal_cost = topology[router_id][i]; // Original Cost from router_id to router i
            for(int j=1; j<=max_routing_id; j++){
                if(j != i && j != router_id) {
                    int new_cost = topology[router_id][j] + topology[j][i];
                    if(new_cost < orginal_cost) {
                        topology[router_id][i] = new_cost;
                        //need to update routing_table_here
                        routing_table[i][0] = j;
                        routing_table[i][1] = new_cost;
                    }
                }
            }
            
        }
    }
}

void print_topology(int max_id) {
    for(int i=1; i<=max_id; i++) {
        for(int j=1; j<=max_id; j++) {
            printf("%d\t", topology[i][j]);
        }
        printf("\n");
    }
}

void print_routing_table(int max_id) {
    for(int i=1; i<=max_id; i++) {
        printf("dest_id: %d \t next hop: %d \t cost: %d \n", i, routing_table[i][0], routing_table[i][1]);
    }
}