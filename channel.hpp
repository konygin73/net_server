#ifndef CHANNEL_CLASS_HPP
#define CHANNEL_CLASS_HPP

#include <mutex>
#include <condition_variable>
#include "capacity.hpp"

class Packet;
class QueuePacket;

class Channel {
    static const int BUF_COUNT = 100;
public:
    Channel(uint8_t num, QueuePacket *parent);
    
    void movePacket(Packet *packet);//переместить пакет в общую очередь

    Packet* getFreePacket();
    void returnFreePacket(Packet *packet);

    Capacity::CapacityVolume getCapacity(uint32_t timestamp);
    uint32_t getVolume(uint32_t startTimestamp, uint32_t endTimestamp);
    uint8_t getNum();

    void done();
    bool isDone() const;

private:
    uint8_t m_num;
    QueuePacket *m_parent;
    bool m_proc_done;
    Capacity m_capacity;
};

#endif //CHANNEL_CLASS_HPP
