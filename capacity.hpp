#ifndef CAPACITY_CLASS_HPP
#define CAPACITY_CLASS_HPP

#include <cstdint>
#include <mutex>
#include <map>

const uint32_t PACK_HEADER = 0xAACCAACC;

class Capacity {
public:
    struct CapacityVolume {
        uint32_t timestamp;
        uint32_t volume;
        uint32_t count;
        CapacityVolume(): timestamp{0}, volume{0}, count{0} {}
    };

    #pragma pack(push, 1)
    struct LossesPacket {
        uint32_t timestamp;
        float losses;
        float speed;
        LossesPacket(): timestamp{0}, losses{-1.0f}, speed{0} {}
    };
    #pragma pack(pop)

    #pragma pack(push, 1)
    struct CapacityInterval {
        uint32_t header;
        uint32_t startTimestamp;
        uint32_t endTimestamp;
        uint32_t sendVolume;
        uint32_t lossVolume;
        CapacityInterval(): header{PACK_HEADER}, startTimestamp{0}, 
            endTimestamp{0}, sendVolume{0}, lossVolume{0} {}
    };
    #pragma pack(pop)

    Capacity(uint8_t num);

    void appendTraffic(uint32_t timestamp, uint16_t volume);
    CapacityVolume getCapacity(uint32_t timestamp);
    uint32_t getVolume(uint32_t startTimestamp, uint32_t endTimestamp);

private:
    std::map<uint32_t, CapacityVolume> m_timeSt;
    mutable std::mutex m_mtxTimeSt;

    uint8_t m_num;
};

#endif //CAPACITY_CLASS_HPP
