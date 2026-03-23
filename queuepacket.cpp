#include <string>
#include <cstring>
#include <arpa/inet.h>
#include "queuepacket.hpp"
#include "packet.hpp"

QueuePacket::QueuePacket(): 
    m_channels{{0, this}, {1, this}},//m_channels{{this}}
    m_proc_done{false}
{
    memset(m_timeBuff, 0, sizeof(m_timeBuff));
}

Channel* QueuePacket::getChannel(int num)
{
    return &m_channels[num];
}

Packet* QueuePacket::getFreePacket()
{
    std::unique_lock<std::mutex> lock(m_mtxFreeBuffers);
    if(m_freeBuffers.empty())
    {
        printf("new packet\n");
        return new Packet;
    }
    Packet *packet = m_freeBuffers.back();
    m_freeBuffers.pop_back();
    return packet;
}

void QueuePacket::returnFreePacket(Packet *packet)
{
    std::unique_lock<std::mutex> lock(m_mtxFreeBuffers);
    m_freeBuffers.push_back(packet);
}

void QueuePacket::movePacket(Packet *packet)
{
    uint32_t timestamp = packet->getTimestamp();
    uint16_t num = packet->getNum();
    
    {
        std::unique_lock<std::mutex> lock(m_mtxTimeBuff);
        if(m_timeBuff[num] == timestamp)
        {
            returnFreePacket(packet);
            return;
        }
        m_timeBuff[num] = timestamp;
    }

    {
        std::unique_lock<std::mutex> lock(m_mtxQueue);
        uint64_t key = timestamp;
        key = (key << 32) + num;
        m_map[key] = packet;
    }
    m_cvQueue.notify_one();
}

Packet* QueuePacket::getForSend(uint16_t next)
{
    std::unique_lock<std::mutex> lock(m_mtxQueue);
    m_cvQueue.wait(lock, [this] { return m_map.size() > MAP_SIZE || m_proc_done; });
    if(m_proc_done)
    {
        return nullptr;
    }

    auto it = m_map.begin();
    Packet *packet = it->second;
    uint16_t num = packet->getNum();
    while(next > num)
    {
        printf("\nold-%u ", num);
        m_map.erase(it);
        it = m_map.begin();
        packet = it->second;
        num = packet->getNum();
    }
    if(num != next)
    {
        printf("\nNF-%u ", next);
        //auto i = it;
        //for(;i != m_map.end(); ++i)
        //{
        //    printf(":%u ", i->second->getNum());
        //}
        //printf("\nsize: %lu\n", m_map.size());
    }
    //else
    //{
    //    printf("+%u ", it->second->getNum());
    //}
    m_map.erase(it);
    return packet;
}

/*
void QueuePacket::movePacket(Packet *packet)
{
    uint32_t timestamp = packet->getTimestamp();
    uint16_t num = packet->getNum();
    
    {
        std::unique_lock<std::mutex> lock(m_mtxTimeBuff);
        if(m_timeBuff[num] == timestamp)
        {
            returnFreePacket(packet);
            return;
        }
        m_timeBuff[num] = timestamp;
    }

    {
        std::unique_lock<std::mutex> lock(m_mtxQueue);
        m_queue.push(packet);
    }
    m_cvQueue.notify_one();
}

Packet* QueuePacket::getForSend()
{
    std::unique_lock<std::mutex> lock(m_mtxQueue);
    m_cvQueue.wait(lock, [this] { return !m_queue.empty() || m_proc_done; });
    if(m_proc_done)
    {
        return nullptr;
    }

    Packet *packet = m_queue.front();
    m_queue.pop();
    return packet;
}
*/
void QueuePacket::done()
{
    for(int i = 0; i < AMOUNT_CHANNELS; ++i)
    {
        m_channels[i].done();
    }
    m_proc_done = true;
    m_cvQueue.notify_one();
}

bool QueuePacket::isExist(const Packet *packet)
{
    uint16_t num = packet->getNum();
    uint32_t timestamp = packet->getTimestamp();
    std::unique_lock<std::mutex> lock(m_mtxTimeBuff);
    if(m_timeBuff[num] == timestamp)
    {
        return true;
    }
    m_timeBuff[num] = timestamp;
    return false;
}
