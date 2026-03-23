#include <string>
#include <chrono>

#include "capacity.hpp"

Capacity::Capacity(uint8_t num): m_num{num}
{
}

void Capacity::appendTraffic(uint32_t timestamp, uint16_t volume)
{
    std::unique_lock<std::mutex> lock(m_mtxTimeSt);
    CapacityVolume &capacityVolume = m_timeSt[timestamp];
    capacityVolume.count++;
    capacityVolume.volume += volume;

    if(m_timeSt.size() > 600)//не больше 600 записей
    {
        auto it = m_timeSt.end();
        std::advance(it, -300);//оставляем последние 300 записей
        m_timeSt.erase(m_timeSt.begin(), it);
    }
}

Capacity::CapacityVolume Capacity::getCapacity(uint32_t timestamp)
{
    std::unique_lock<std::mutex> lock(m_mtxTimeSt);
    auto it = m_timeSt.find(timestamp);
    if(it == m_timeSt.end())
    {
        return Capacity::CapacityVolume();
    }
    return it->second;
}

// > startTimestamp, <= endTimestamp
uint32_t Capacity::getVolume(uint32_t startTimestamp, uint32_t endTimestamp)
{
    std::unique_lock<std::mutex> lock(m_mtxTimeSt);
    auto it = m_timeSt.upper_bound(startTimestamp);
    uint32_t volume = 0;
    if(it != m_timeSt.end())
    {
        printf("start found %u\n", it->first);
    }
    uint32_t end = 0;
    uint32_t count = 0;
    while(it != m_timeSt.end() && it->first <= endTimestamp)
    {
        volume += it->second.volume;
        end = it->first;
        count += it->second.count;
        ++it;
    }
    printf("end found %u, count %u\n", end, count);
    if(volume > 0 && it == m_timeSt.end())
    {
        printf("end no found %u\n", m_num);
    }
    return volume;
}
