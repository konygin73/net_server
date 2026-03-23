#include <string>
#include <cstring>
#include <arpa/inet.h>
#include "packet.hpp"
#include "channel.hpp"
#include "queuepacket.hpp"

Channel::Channel(uint8_t num, QueuePacket *parent):
    m_num{num},
    m_parent{parent},
    m_proc_done{false},
    m_capacity{num}
{
}

uint8_t Channel::getNum()
{
    return m_num;
}

void Channel::movePacket(Packet *packet)
{
    m_capacity.appendTraffic(packet->getTimestamp(), packet->getDataLen());
    m_parent->movePacket(packet);
}

Capacity::CapacityVolume Channel::getCapacity(uint32_t timestamp)
{
    return m_capacity.getCapacity(timestamp);
}

uint32_t Channel::getVolume(uint32_t startTimestamp, uint32_t endTimestamp)
{
    return m_capacity.getVolume(startTimestamp, endTimestamp);
}

void Channel::done()
{
    m_proc_done = true;
    printf("Set channel proc done\n");
}

bool Channel::isDone() const
{
    
    return m_proc_done;
}

Packet* Channel::getFreePacket()
{
    return m_parent->getFreePacket();
}

void Channel::returnFreePacket(Packet *packet)
{
    m_parent->returnFreePacket(packet);
}
