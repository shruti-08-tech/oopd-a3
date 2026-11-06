
#pragma once

#include "Config.h"
#include "Model.h"

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

    Counts updateRows(
        std::size_t beginRow,
        std::size_t endRow,
        unsigned day,
        const std::array<double, 9>& infectionProb
    );

    std::uint64_t checksum() const;

    void updateLockdown(const Counts& totals);

    double effectiveBeta() const;

    void saveSnapshot(unsigned day) const;
};
