
#ifndef NET_SIM_H
#define NET_SIM_H

#include <functional>
#include <bitset>
#include <thread>
#include <map>
#include <list>
#include <mutex>

#include <inttypes.h>
#include <stdlib.h>
#include <time.h>

#include <Windows.h>
int64_t NowMs()
{
    return GetTickCount();
}

class Packet
{
public:
    Packet(uint8_t *data_, size_t len_)
    {
        data = new uint8_t[len_];
        memcpy(data, data_, len_);
        len = len_;
    }
    ~Packet() { delete []data; }

    uint8_t *data;
    size_t len;
};

class PacketLost
{
public:
    PacketLost() : intv_(100), lost_(0), lost_bitmap_()
    {
        srand((unsigned int)time(NULL));
    }

    void Set(int lost)
    {
        lost_ = lost;
        UpdateLostBitmap();
    }

    bool Run()
    {
        intv_--;
        bool lost = lost_bitmap_[intv_];
        if (intv_ = 0)
        {
            intv_ = 100;
            UpdateLostBitmap();
        }
    }

private:
    void UpdateLostBitmap()
    {
        lost_bitmap_.reset();
        for (int i = 0; i < lost_; i++)
        {
            int idx = rand() % 100;
            lost_bitmap_.set(idx, 1);
        }
    }

    int intv_;
    int lost_;
    std::bitset<100> lost_bitmap_;
};

class NetSim
{
public:
    typedef std::function<void(uint8_t *data, size_t len)> PacketSink;

    NetSim(PacketSink sink)
      : sink_(sink)
      , lost_()
      , delay_(0)
      
      , pending_packets_()
      , pending_packets_lock_()

      , tx_run_(false)
      , tx_worker_()
    {}
    ~NetSim() {}

    void SetLinkLost(int perc) { lost_.Set(perc); }

    void SetLinkDelay(int delay) { delay_ = delay; }

    void Start()
    {
        if (tx_run_)
        {
            tx_run_ = true;
            tx_worker_ = std::thread(std::bind(&NetSim::SendRoutine, this));
        }
        return;
    }

    void Stop()
    {
        if (tx_run_)
        {
            tx_run_ = false;
            tx_worker_.join();
        }

        return;
    }

    bool Input(uint8_t *data, size_t len)
    {
        if (lost_.Run()) { return false; }

        if (delay_ > 0)
        {
            Packet *pkt = new Packet(data, len);
            int64_t ts = NowMs() + delay_;
            {
                std::lock_guard<std::mutex> guard(pending_packets_lock_);
                pending_packets_.insert({ts, pkt});
            }
        }
        else
        {
            sink_(data, len);
        }

        return true;
    }

private:
    void SendRoutine()
    {
        while (tx_run_)
        {
            pending_packets_lock_.lock();
            if (!pending_packets_.empty())
            {
                std::list<Packet*> packets;

                int64_t now = NowMs();
                auto it = pending_packets_.begin();
                for (; it != pending_packets_.end(); it++)
                {
                    if (it->first <= now)
                    {
                        packets.push_back(it->second);
                    }
                }
                pending_packets_.erase(pending_packets_.begin(), it);
                pending_packets_lock_.unlock();

                for (auto it1 = packets.begin(); it1 != packets.end(); it1++)
                {
                    Packet *pkt = *it1;
                    sink_(pkt->data, pkt->len);
                    delete pkt;
                }
            }
            else
            {
                Sleep(5);
                pending_packets_lock_.unlock();
            }
        }
    }

    PacketSink sink_;
    PacketLost lost_;
    int delay_;

    std::multimap<int64_t, Packet*> pending_packets_;
    std::mutex pending_packets_lock_;

    bool tx_run_;
    std::thread tx_worker_;
};

#endif
