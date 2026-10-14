#pragma once

#include <cstdint>
#include <functional>
#include <condition_variable>
#include <thread>
#include <mutex>
#include <vector>

namespace mindone {

class RemovalBatch {
  public:
    using Remove = std::function<bool(int32_t)>;
    using Complete = std::function<void(const std::vector<int32_t>&)>;

    RemovalBatch() : worker_([this] { run(); }) {}
    ~RemovalBatch() {
        {
            std::lock_guard<std::mutex> guard(lock_);
            stopping_ = true;
            active_ = false;
        }
        changed_.notify_all();
        worker_.join();
    }

    bool start(const std::vector<int32_t>& ids, Remove remove, Complete complete) {
        {
            std::lock_guard<std::mutex> guard(lock_);
            if (active_ || retired_) return false;
            active_ = true;
            pending_ = ids;
            removed_.clear();
            index_ = 0;
            waiting_ = false;
            remove_ = std::move(remove);
            complete_ = std::move(complete);
        }
        changed_.notify_all();
        return true;
    }

    void result(int32_t id, uint32_t remaining) {
        {
            std::lock_guard<std::mutex> guard(lock_);
            if (!active_ || !waiting_) return;
            if (id != 0 && pending_[index_] != 0 && id != pending_[index_]) return;
            if (id != 0) removed_.push_back(id);
            if (remaining != 0) return;
            waiting_ = false;
            ++index_;
        }
        changed_.notify_all();
    }

    bool fail() {
        {
            std::lock_guard<std::mutex> guard(lock_);
            if (!active_) return false;
            waiting_ = false;
            index_ = pending_.size();
        }
        changed_.notify_all();
        return true;
    }

    void cancel() {
        std::lock_guard<std::mutex> dispatch(dispatch_);
        std::lock_guard<std::mutex> guard(lock_);
        active_ = false;
        retired_ = true;
        complete_ = {};
        remove_ = {};
    }

  private:
    void run() {
        std::unique_lock<std::mutex> guard(lock_);
        while (!stopping_) {
            changed_.wait(guard, [this] { return stopping_ || (active_ && !waiting_); });
            if (stopping_) break;
            if (index_ == pending_.size()) {
                auto complete = std::move(complete_);
                auto removed = std::move(removed_);
                active_ = false;
                remove_ = {};
                guard.unlock();
                if (complete) complete(removed);
                guard.lock();
                continue;
            }
            auto remove = remove_;
            auto index = index_;
            auto id = pending_[index];
            guard.unlock();
            std::unique_lock<std::mutex> dispatch(dispatch_);
            guard.lock();
            if (!active_ || stopping_ || index_ != index) {
                dispatch.unlock();
                continue;
            }
            waiting_ = true;
            guard.unlock();
            bool accepted = remove(id);
            guard.lock();
            if (active_ && waiting_ && index_ == index && !accepted) {
                waiting_ = false;
                ++index_;
            }
            dispatch.unlock();
        }
    }

    std::mutex lock_;
    std::mutex dispatch_;
    bool active_ = false;
    bool waiting_ = false;
    bool retired_ = false;
    bool stopping_ = false;
    size_t index_ = 0;
    std::vector<int32_t> pending_;
    std::vector<int32_t> removed_;
    Remove remove_;
    Complete complete_;
    std::condition_variable changed_;
    std::thread worker_;
};

}
