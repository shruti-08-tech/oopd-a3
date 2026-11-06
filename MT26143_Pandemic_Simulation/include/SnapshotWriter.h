
#pragma once

#include "Config.h"
#include "Model.h"

#include <deque>
#include <filesystem>
#include <future>
#include <vector>

class SnapshotWriter {
public:
    explicit SnapshotWriter(const Config& config);

    void submit(
        unsigned day,
        const std::vector<Cell>& grid
    );

    // Wait for all outstanding snapshots.
    // Propagate the first failure after draining.
    void finish();

private:
    std::size_t n_;
    unsigned latencyMs_;
    unsigned maxPending_;

    std::string mode_;
    std::filesystem::path directory_;

    std::deque<std::future<void>> pending_;

    static void writePpm(
        std::filesystem::path path,
        std::size_t n,
        unsigned latencyMs,
        const std::vector<Cell>& grid
    );
};
