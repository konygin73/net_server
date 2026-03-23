#ifndef QUEUEPACKET_CLASS_HPP
#define QUEUEPACKET_CLASS_HPP

#include <mutex>
#include <condition_variable>
//#include <chrono>
#include <vector>
//#include <queue>
#include <map>
#include "channel.hpp"

const uint8_t MAP_SIZE = 120;//2 sec
class Packet;

class QueuePacket {
    static const int AMOUNT_CHANNELS = 2;//число каналов связи
    static const int BUF_COUNT = 100;//размер очереди буферов на отправку
public:
    QueuePacket();

    bool isExist(const Packet *packet);//проверить на дубли и добавить, если нет
    void done();//завершить работу
    
    Packet* getFreePacket();//забрать пустой пакет
    void returnFreePacket(Packet *packet);//вернуть пакет

    void movePacket(Packet *packet);//переместить пакет в очередь на отправку
    Packet* getForSend(uint16_t next);//взять пакет из очереди

    Channel* getChannel(int num);

private:
    uint32_t m_timeBuff[0xFFFF];
    mutable std::mutex m_mtxTimeBuff;

    Channel m_channels[AMOUNT_CHANNELS];

    //std::queue<Packet*> m_queue;//очередь буферов на отправку
    std::map<uint64_t, Packet*> m_map;//очередь буферов на отправку
    mutable std::mutex m_mtxQueue;
    mutable std::condition_variable m_cvQueue;

    std::vector<Packet*> m_freeBuffers;//хранилище пустых буферов
    mutable std::mutex m_mtxFreeBuffers;

    bool m_proc_done;
};
#endif //QUEUEPACKET_CLASS_HPP
