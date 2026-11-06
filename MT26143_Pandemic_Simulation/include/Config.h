
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct Config {
    std::size_t gridSize = 1000;
    unsigned maxDays = 200;
    std::uint64_t seed = 42;

    double beta = 0.08;
    unsigned infectiousDays = 7;
    double mortality = 0.01;

    std::size_t initiallyInfected = 10;
    double vaccinatedFraction = 0.0;

    double maskFactor = 1.0;
    double lockdownFactor = 0.3;
    double lockdownOn = 0.02;
    double lockdownOff = 0.005;

    // Options reserved for Parts B and C.
    unsigned threads = 1;
    unsigned chunks = 1;
    std::string partition = "strips";

    unsigned snapshotEvery = 0;
    std::string snapshotMode = "blocking";
    std::string snapshotDir = "snapshots";
    unsigned maxPendingWrites = 2;
    unsigned ioLatencyMs = 0;

    bool fixedDays = false;
    std::string csvPath = "results/sequential.csv";
};

Config parseArgs(int argc, char* argv[]);
