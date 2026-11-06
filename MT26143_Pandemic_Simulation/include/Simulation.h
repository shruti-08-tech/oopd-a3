
#pragma once

#include "Config.h"
#include "Model.h"
#include "Partition.h"

#include <array>
#include <cstdint>
#include <vector>

class Simulation {
public:
    explicit Simulation(Config config);
    void run();

private:
    Config config_;

    std::vector<Cell> current_;
    std::vector<Cell> next_;

    bool lockdownActive_ = false;

    void initialize();

    Counts countCurrent() const;

    Counts updateChunk(
        const Chunk& chunk,
        unsigned day,
        const std::array<double, 9>& infectionProb
    );

    Counts updateSpawn(
        const std::vector<Chunk>& chunks,
        unsigned day,
        const std::array<double, 9>& infectionProb
    );

    std::uint64_t checksum() const;

    void updateLockdown(const Counts& totals);

    double effectiveBeta() const;

    void saveSnapshot(unsigned day) const;
};
