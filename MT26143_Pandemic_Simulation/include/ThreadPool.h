
#pragma once

#include "Model.h"
#include "Partition.h"

#include <array>
#include <functional>
#include <future>
#include <thread>
#include <vector>

class ThreadPool {
public:
    using Probabilities = std::array<double, 9>;

    using UpdateFunction = std::function<
        Counts(
            const Chunk&,
            unsigned,
            const Probabilities&
        )
    >;

    ThreadPool(
        unsigned workers,
        UpdateFunction update
    );

    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    Counts execute(
        const std::vector<Chunk>& chunks,
        unsigned day,
        const Probabilities& probabilities
    );

private:
    struct Slot {
        std::vector<Chunk> chunks;
        unsigned day = 0;
        Probabilities probabilities{};

        std::future<void> nextNotification;
        std::promise<Counts> done;

        bool stop = false;
    };

    UpdateFunction update_;

    std::vector<Slot> slots_;
    std::vector<std::promise<void>> notifications_;
    std::vector<std::thread> workers_;
};
