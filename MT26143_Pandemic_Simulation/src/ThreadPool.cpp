
#include "ThreadPool.h"

#include <exception>
#include <stdexcept>
#include <utility>

ThreadPool::ThreadPool(
    unsigned workers,
    UpdateFunction update
)
    : update_(std::move(update)),
      slots_(workers),
      notifications_(workers) {

    if (workers == 0) {
        throw std::invalid_argument(
            "Thread pool cannot be empty"
        );
    }

    workers_.reserve(workers);

    try {
        for (unsigned i = 0; i < workers; ++i) {
            auto first = notifications_[i].get_future();

            workers_.emplace_back(
                [this, i, notification = std::move(first)]()
                mutable {
                    Slot& slot = slots_[i];

                    for (;;) {
                        try {
                            notification.get();
                        }
                        catch (...) {
                            return;
                        }

                        if (slot.stop)
                            return;

                        notification =
                            std::move(slot.nextNotification);

                        std::promise<Counts> completion =
                            std::move(slot.done);

                        try {
                            Counts local;

                            for (const Chunk& chunk : slot.chunks) {
                                local += update_(
                                    chunk,
                                    slot.day,
                                    slot.probabilities
                                );
                            }

                            completion.set_value(local);
                        }
                        catch (...) {
                            completion.set_exception(
                                std::current_exception()
                            );
                        }
                    }
                }
            );
        }
    }
    catch (...) {
        for (std::size_t i = 0;
             i < workers_.size();
             ++i) {
            slots_[i].stop = true;
            notifications_[i].set_value();
        }

        for (auto& worker : workers_) {
            if (worker.joinable())
                worker.join();
        }

        throw;
    }
}

ThreadPool::~ThreadPool() {
    for (std::size_t i = 0;
         i < workers_.size();
         ++i) {
        slots_[i].stop = true;
        notifications_[i].set_value();
    }

    for (auto& worker : workers_) {
        if (worker.joinable())
            worker.join();
    }
}

Counts ThreadPool::execute(
    const std::vector<Chunk>& chunks,
    unsigned day,
    const Probabilities& probabilities
) {
    const std::size_t T = workers_.size();

    std::vector<std::promise<void>> nextNotifications(T);

    std::vector<std::future<Counts>> results;
    results.reserve(T);

    // Prepare the next day's work.
    for (std::size_t i = 0; i < T; ++i) {
        Slot& slot = slots_[i];

        slot.chunks.clear();
        slot.day = day;
        slot.probabilities = probabilities;
        slot.done = std::promise<Counts>{};

        results.push_back(slot.done.get_future());

        slot.nextNotification =
            nextNotifications[i].get_future();
    }

    // Distribute C chunks among T workers.
    // This supports C > T.
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        slots_[i % T].chunks.push_back(chunks[i]);
    }

    // Notify workers to begin processing.
    for (auto& signal : notifications_) {
        signal.set_value();
    }

    notifications_.swap(nextNotifications);

    // Wait for all workers before returning.
    Counts totals;
    std::exception_ptr firstException;

    for (auto& result : results) {
        try {
            totals += result.get();
        }
        catch (...) {
            if (!firstException)
                firstException = std::current_exception();
        }
    }

    if (firstException)
        std::rethrow_exception(firstException);

    return totals;
}
