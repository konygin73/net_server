#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <signal.h>
#include <chrono>
#include <string>
#include <fcntl.h>
#include <sstream>

#include "queuepacket.hpp"
#include "packet.hpp"

volatile bool gInterrupted = false;
QueuePacket *g_queuePacket;

void printChar(const char *data, uint32_t size)
{
    static char buf[] = {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
    for(uint32_t i = 0; i < size; ++i)
    {
        uint8_t dd = *((uint8_t*)data + i);
        uint8_t dd1 = dd >> 4;
        dd = dd & 0xF;
        dd1 = dd1 & 0xF;
        printf("%c%c \n", buf[dd1], buf[dd]);
    }
}

// Обработчик сигнала SIGINT
void signalHandler(int signum) {
    printf("\nПерехвачен сигнал SIGINT (%d)!\n", signum);
    gInterrupted = true; //Устанавливаем флаг для завершения программы
    if(g_queuePacket != nullptr)
    {
        g_queuePacket->done();
    }
}

void taskTCP(Channel *channel, const char *addr, uint16_t port)
{
    int sockTCP = 0;
    int clientTCP = 0;
    struct sockaddr_in serverTCPaddr;

    memset(&serverTCPaddr, 0, sizeof(serverTCPaddr));
    serverTCPaddr.sin_family = AF_INET;
    if(inet_pton(AF_INET, addr, &serverTCPaddr.sin_addr) != 1)
    {
        printf("err TCP addr no convert: %s", addr);
        exit(EXIT_FAILURE);
    }
    serverTCPaddr.sin_port = htons(port);

    while(!channel->isDone())
    {
        if(sockTCP <= 0)
        {
            sockTCP = socket(AF_INET, SOCK_STREAM, 0);//IPPROTO_TCP
            if(sockTCP < 0)
            {
                perror("TCP socket creation failed");
                exit(EXIT_FAILURE);
            }
            printf("create tcp socket\n");
        }

        int opt = 1;
        if(setsockopt(sockTCP, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) < 0)
        {
            perror("Tcp setsockopt failed\n");
            close(sockTCP);
            sockTCP = 0;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        // Bind the socket to localhost port
        if(bind(sockTCP, (struct sockaddr*)&serverTCPaddr, sizeof(serverTCPaddr)) < 0)
        {
            perror("bind failed");
            close(sockTCP);
            sockTCP = 0;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        std::cout << "Listener on port: " << port << std::endl;

        // Try to specify backlog of 3 pending connections
        if(listen(sockTCP, 3) < 0)
        {
            perror("listen");
            close(sockTCP);
            sockTCP = 0;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        
        fd_set rfds;
        fd_set wfds;
        fd_set efds;
        struct timeval tv;
        Packet *packet = nullptr;
        int max;
        Capacity::LossesPacket lossesPacket;
        while (!channel->isDone())
        {
            max = sockTCP;

            FD_ZERO(&rfds);
            FD_ZERO(&wfds);
            FD_ZERO(&efds);

            if(clientTCP == 0)//for connecting
            {
                FD_SET(sockTCP, &rfds);
            }

            if(clientTCP > 0)
            {
                FD_SET(clientTCP, &rfds);
                FD_SET(clientTCP, &efds);
                if(clientTCP > max)
                {
                    max = clientTCP;
                }
                //есть кому и что передавать
                if(lossesPacket.losses >= 0)
                {
                    FD_SET(clientTCP, &wfds);
                }
            }

            FD_SET(sockTCP, &efds);

            tv.tv_sec = 0; tv.tv_usec = 1000;
            int ret = select(max + 1, &rfds, &wfds, &efds, &tv);

            if(ret == 0)
            {
                continue;
            }

            if(ret < 0)
            {
                perror("select error");
                close(sockTCP);
                sockTCP = 0;
                break;
            }

            //server is err
            if(FD_ISSET(sockTCP, &efds))
            {
                int saved_errno = errno;
                printf("error TCP socket: %s\n", strerror(saved_errno));
                close(sockTCP);
                sockTCP = 0;
                break;
            }

            //server is readable to connect
            if(FD_ISSET(sockTCP, &rfds))
            {
                struct sockaddr_in address;
                memset(&address, 0, sizeof(address));
                int addrlen = sizeof(address);
                if((clientTCP = accept(sockTCP, (struct sockaddr*)&address, (socklen_t *)&addrlen)) < 0)
                {
                    perror("accept");
                }
                else
                {
                    std::cout << "New connection, ip is: " << inet_ntoa(address.sin_addr) << 
                        ", port : " << ntohs(address.sin_port) << std::endl;
                }
            }

            //server is writable
            if(lossesPacket.losses >= 0 && clientTCP > 0 && FD_ISSET(clientTCP, &wfds))
            {
                ssize_t ret = send(clientTCP, &lossesPacket, sizeof(lossesPacket), 0);//!MSG_DONTWAIT
                if(ret < 0)
                {
                    int saved_errno = errno;
                    printf("Error send TCP data: %s\n", strerror(saved_errno));
                    break;
                }
                if(ret != sizeof(lossesPacket))
                {
                    printf("Not all send TCP size %ld != %lu\n", ret, sizeof(lossesPacket));
                }
                else
                {
                    printf("Send TCP: %u %.2f %.2f\n", channel->getNum(), lossesPacket.losses, lossesPacket.speed);
                }
                lossesPacket.losses = -1.0f;
            }

            if(clientTCP > 0 && FD_ISSET(clientTCP, &rfds))
            {
                Capacity::CapacityInterval clientCapacityInterval;
                ssize_t received_count = recv(clientTCP, &clientCapacityInterval,
                    sizeof(clientCapacityInterval), MSG_DONTWAIT);
                if(received_count == 0)
                {
                    printf("Close TCP connection\n");
                    break;
                }
                else if(received_count < 0)
                {
                    int saved_errno = errno; // Save errno immediately
                    printf("Error TCP data: %s\n", strerror(saved_errno));
                    break;
                }
                else if(received_count == sizeof(clientCapacityInterval))
                {
                    if(clientCapacityInterval.header != PACK_HEADER)
                    {
                        printf("TCP recv header error\n");    
                    }
                    else
                    {
                        //printf("\nTCP%u recv %u->%u\n", channel->getNum(), clientCapacityInterval.startTimestamp, 
                        //    clientCapacityInterval.endTimestamp);
                        uint32_t receivedVolume = channel->getVolume(clientCapacityInterval.startTimestamp, 
                            clientCapacityInterval.endTimestamp);
                        if(receivedVolume == 0)
                        {
                            printf("no search interval %u->%u\n", clientCapacityInterval.startTimestamp, 
                                clientCapacityInterval.endTimestamp);
                        }
                        else
                        {
                            float interval = (clientCapacityInterval.endTimestamp - 
                                clientCapacityInterval.startTimestamp) / 90000.0f;//sec
                            float speed = (float)receivedVolume / interval;
                            printf("\nchannel%u received data rate: %.2f\n", channel->getNum(), speed);

                            lossesPacket.timestamp = clientCapacityInterval.endTimestamp;
                            uint32_t lossesVolume = clientCapacityInterval.sendVolume - receivedVolume + clientCapacityInterval.lossVolume;
                            lossesPacket.losses = 1.0f - (float)lossesVolume / clientCapacityInterval.sendVolume;

                            //printf("client send volue: %u, loss volue: %u, server received volume: %u, losses: %.2f\n",
                            //    clientCapacityInterval.sendVolume, clientCapacityInterval.lossVolume, receivedVolume, 
                            //    lossesPacket.losses);
                            printf("channel%u received volume: %u, losses: %u on delta_time %.2f sec\n",
                                channel->getNum(), receivedVolume, lossesVolume, interval);
                            //    clientCapacityInterval.sendVolume, clientCapacityInterval.lossVolume, receivedVolume, 
                            //    lossesPacket.losses);
                        }
                    }
                }
                else
                {
                    printf("Read TCP short size: %ld\n", received_count);
                }
            }
        }
        if(sockTCP > 0)
        {
            close(sockTCP);
            sockTCP = 0;
        }
        if(clientTCP > 0)
        {
            close(clientTCP);
            clientTCP = 0;
        }
        printf("reconnect TCP\n");
    }
    printf("exit TCP port: %d\n", port);
}

void taskUDP(Channel *channel, const char *bind_addr, uint16_t bind_port)
{
    int sockUDP = 0;
    struct sockaddr_in serverUDPaddr;

    //Prepare UDP address structure
    memset(&serverUDPaddr, 0, sizeof(serverUDPaddr));
    serverUDPaddr.sin_family = AF_INET;
    if(inet_pton(AF_INET, bind_addr, &serverUDPaddr.sin_addr) != 1)
    {
        printf("err UDP addr no convert: %s", bind_addr);
        exit(EXIT_FAILURE);
    }
    serverUDPaddr.sin_port = htons(bind_port);
    Packet *packet = nullptr;

    while(!channel->isDone())
    {
        if(sockUDP <= 0)
        {
            sockUDP = socket(AF_INET, SOCK_DGRAM, 0);//IPPROTO_UDP
            if(sockUDP < 0)
            {
                perror("UDP socket creation failed");
                exit(EXIT_FAILURE);
            }
            printf("create udp socket\n");
        }

        int opt = 1;
        if(setsockopt(sockUDP, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0)
        {
            perror("UDP setsockopt failed\n");
            close(sockUDP);
            sockUDP = 0;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        struct timeval timeout;
        timeout.tv_sec = 1;  // 1 seconds
        timeout.tv_usec = 0; // 0 microseconds
        if(setsockopt(sockUDP, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
        {
            perror("setsockopt failed");
            exit(EXIT_FAILURE);
        }

        // 3. Bind the socket to the server address
        if(bind(sockUDP, (const struct sockaddr*)&serverUDPaddr, sizeof(serverUDPaddr)) < 0)
        {
            perror("bind failed");
            close(sockUDP);
            exit(EXIT_FAILURE);
        }

        printf("UDP server binding on port %d\n", bind_port);

        int printNum = 0;
        while (!gInterrupted)
        {
            if(packet == nullptr)
            {
                packet = channel->getFreePacket();
            }
            socklen_t addr_len = sizeof(bind_addr);
            ssize_t bytes_received = recvfrom(sockUDP, packet->m_data, PACK_BUFFER_SIZE, 0,
                (struct sockaddr*)&bind_addr, &addr_len);
            if(bytes_received < 0)
            {
                if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                {
                    printf("continue ");
                    continue;
                }
                else
                {
                    perror("recvfrom failed");
                    break;
                }
            }
            else if (bytes_received > 0)
            {
                //uint16_t num = packet->getNum();
                //uint32_t timestamp = packet->getTimestamp();
                //printf("%d->%x %x %ld\n", bind_port, timestamp, num, bytes_received);

                packet->setLen(bytes_received);//размер пакета
                uint16_t num = packet->getNum();
                //printf("+%u ", num);
                channel->movePacket(packet);
                packet = nullptr;
            }
        }
        if(sockUDP > 0)
        {
            close(sockUDP);
            sockUDP = 0;
        }
        
        printf("reconnect\n");
    }
    printf("exit UDP port: %d\n", bind_port);
}

int main(int argc, char *argv[])
{
    std::string addrOut;
    uint16_t portOut;

    std::string addr0;
    uint16_t port0;

    std::string addr1;
    uint16_t port1;

    if(argc != 4)
    {
        printf("start with parameters: 192.168.1.210:30001 192.168.1.210:20001 192.168.1.210:20003\n");
        printf("адрес для посылки udp объединенных каналов\n");
        printf("адрес для прослушивания udp на 0-й канал 20001\n");
        printf("адрес для прослушивания udp на 1-й канал\n");
        printf("адрес для прослушивания TCP на 0-й канал 20001 + 1\n");
        printf("адрес для прослушивания udp на 1-й канал\n");
        printf("адрес для прослушивания TCP на 1-й канал 20003 + 1\n");

        addrOut = "192.168.1.210";
        portOut = 10001;

        addr0 = "192.168.1.210";
        port0 = 20001;

        addr1 = "192.168.1.210";
        port1 = 20003;
    }
    else
    {
        for (int i = 1; i < argc; ++i)
        {
            std::vector<std::string> elems;
            std::stringstream ss(argv[i]);
            std::string item;
            while (std::getline(ss, item, ':')) {
                    elems.push_back(item);
            }
            if(elems.size() != 2)
            {
                return -1;
            }
            uint16_t port = std::atoi(elems[1].c_str());
            if(port == 0)
            {
                return -1;
            }

            switch(i)
            {
                case 1:
                    addrOut = elems[0];
                    portOut = port;
                    break;
                case 2:
                    addr0 = elems[0];
                    port0 = port;
                    break;
                case 3:
                    addr1 = elems[0];
                    port1 = port;
                    break;
                default:
                    break;
            }
        }
    }

    QueuePacket queuePacket;
    g_queuePacket = &queuePacket;

    // Устанавливаем обработчик для сигнала SIGINT
    if(signal(SIGINT, signalHandler) == SIG_ERR)
    {
        perror("Ошибка установки обработчика сигнала");
        exit(EXIT_FAILURE);
    }

    std::thread tu1(taskUDP, queuePacket.getChannel(0), addr0.c_str(), port0);
    printf("tu1 = %s:%d\n", addr0.c_str(), port0);

    std::thread tcp1(taskTCP, queuePacket.getChannel(0), addr0.c_str(), port0 + 1);
    printf("tcp1 = %s:%d\n", addr0.c_str(), port0 + 1);

    std::thread tu2(taskUDP, queuePacket.getChannel(1), addr1.c_str(), port1);
    printf("t2 = %s:%d\n", addr1.c_str(), port1);

    std::thread tcp2(taskTCP, queuePacket.getChannel(1), addr1.c_str(), port1 + 1);
    printf("tcp2 = %s:%d\n", addr0.c_str(), port1 + 1);

    int sockfd = 0;
    struct sockaddr_in thisAddr;

    memset(&thisAddr, 0, sizeof(thisAddr));
    thisAddr.sin_family = AF_INET;
    //thisAddr.sin_addr.s_addr = INADDR_ANY; // Listen on all available interfaces
    if(inet_pton(AF_INET, addrOut.c_str(), &thisAddr.sin_addr) != 1)
    {
        printf("err TCP addr no convert: %s", addrOut.c_str());
        exit(EXIT_FAILURE);
    }
    thisAddr.sin_port = htons(portOut);
    printf("out UDP = %s:%d\n", addrOut.c_str(), portOut);

    Packet *packet = nullptr;
    ssize_t bytes_sended = 0;
    uint16_t len = 0;
    uint16_t pack_num = 0;
    while (!gInterrupted)
    {
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);//IPPROTO_UDP
        if(sockfd < 0)
        {
            perror("socket creation failed");
            exit(EXIT_FAILURE);
        }

        struct timeval timeout;
        timeout.tv_sec = 1;  // 1 seconds
        timeout.tv_usec = 0; // 0 microseconds
        if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
        {
            perror("setsockopt failed");
            exit(EXIT_FAILURE);
        }

        int opt = 1;
        if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0)
        {
            perror("UDP setsockopt failed\n");
            close(sockfd);
            sockfd = 0;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        struct sockaddr_in bindAddr;
        memset(&bindAddr, 0, sizeof(bindAddr));
        bindAddr.sin_family = AF_INET;
        if(inet_pton(AF_INET, addrOut.c_str(), &bindAddr.sin_addr) != 1)
        {
            printf("err TCP addr no convert: %s", addrOut.c_str());
            close(sockfd);
            sockfd = 0;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        bindAddr.sin_port = htons(portOut - 1);
        printf("bind out UDP = %s:%d\n", addrOut.c_str(), portOut - 1);

        if(bind(sockfd, (const struct sockaddr*)&bindAddr, sizeof(bindAddr)) < 0)
        {
            perror("bind failed");
            close(sockfd);
            sockfd = 0;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        uint16_t nextNum = 0;
        int countOk = 0;
        int printNum = 0;
        while (!gInterrupted)
        {
            if(packet == nullptr)
            {
                packet = queuePacket.getForSend(nextNum);
                if(packet == nullptr)
                {
                    printf("packet == nullptr\n");
                    continue;
                }
                bytes_sended = 0;
                len = packet->getDataLen();

                uint16_t num = packet->getNum();
                if(num != nextNum)
                {
                    printf("current: %u; should be: %u; ok: %u\n", num, nextNum, countOk);
                    nextNum = num;
                    countOk = 0;
                }
                //else
                //{
                //    printf("send: %u\n", num);
                //}
                ++countOk;
                ++nextNum;
            }
            ssize_t ret = sendto(sockfd, packet->m_data + bytes_sended, len, 0,
                (struct sockaddr*)&thisAddr, sizeof(thisAddr));
            if(ret <= 0)
            {
                if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                {
                    perror("EAGAIN============================================");
                    continue;
                }
                else
                {
                    perror("recvfrom failed====================================================");
                    break;
                }
            }
            else if (ret > 0)
            {
                //len = len - ret;
                bytes_sended += ret;
                uint16_t num = packet->getNum();
                //uint32_t timestamp = packet->getTimestamp();

                //if(len == 0)
                {
                    //printf("%x %x %ld\n", timestamp, num, bytes_sended);
                    if(pack_num != num)
                    {
                        printf("\n!%u ", pack_num);
                    }
                    pack_num = num + 1;
                    //printf("%u ", num);
                    queuePacket.returnFreePacket(packet);
                    packet = nullptr;
                }
                
                //printf("#%ld %u\n", ret, ntohs(thisAddr.sin_port));
                //printf("#");
                //++printNum;
                //if(printNum > 60)
                //{
                //    printf("\n");
                //    printNum = 0;
                //}
                
            }
        }
        if(sockfd > 0)
        {
            close(sockfd);
            sockfd = 0;
        }
    }
    printf("set gInterrupted\n");
    queuePacket.done();

    tu1.join();
    tcp1.join();
    tu2.join();
    tcp2.join();
    return 0;
}
