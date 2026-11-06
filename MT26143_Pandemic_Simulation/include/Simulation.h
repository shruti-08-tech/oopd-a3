
#pragma once

#include "Config.h"
#include "Model.h"
#include "Partition.h"

#include <array>
#include <cstdint>
#include <vector>

struct SimulationSummary {
    double peakInfectedFraction = 0.0;
    unsigned peakDay = 0;

    std::uint64_t totalInfected = 0;
    std::uint64_t totalDeaths = 0;

    unsigned days = 0;
    double loopSeconds = 0.0;
};

class Simulation {
public:
    explicit Simulation(Config config);

    void run();

    // Run without console, snapshots, or daily CSV.
    SimulationSummary runSummary();

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

    SimulationSummary execute(bool publicOutput);
};
