
#include "Simulation.h"
#include "Random.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace fs = std::filesystem;

Simulation::Simulation(Config config)
    : config_(std::move(config)),
      current_(config_.gridSize * config_.gridSize),
      next_(config_.gridSize * config_.gridSize) {}

// Initialize vaccination and infection.
void Simulation::initialize() {
    std::mt19937_64 generator(config_.seed);

    std::vector<std::size_t> indices(current_.size());

    std::iota(
        indices.begin(),
        indices.end(),
        std::size_t{0}
    );

    const std::size_t vaccinated =
        static_cast<std::size_t>(
            std::floor(
                static_cast<long double>(current_.size()) *
                config_.vaccinatedFraction + 0.5L
            )
        );

    const std::size_t selected =
        vaccinated + config_.initiallyInfected;

    // Partial Fisher-Yates shuffle:
    // select people without replacement.
    for (std::size_t i = 0; i < selected; ++i) {
        std::uniform_int_distribution<std::size_t>
            distribution(i, indices.size() - 1);

        std::swap(
            indices[i],
            indices[distribution(generator)]
        );

        Cell& cell = current_[indices[i]];

        cell.state =
            (i < vaccinated) ? State::V : State::I;

        cell.daysInfected = 0;
    }
}

// Count states in current grid.
Counts Simulation::countCurrent() const {
    Counts counts;

    for (const Cell& cell : current_) {
        counts.add(cell.state);
    }

    return counts;
}

// Update one row range using only current_
// and write results only into next_.
Counts Simulation::updateRows(
    std::size_t beginRow,
    std::size_t endRow,
    unsigned day,
    const std::array<double, 9>& probabilities
) {
    Counts totals;

    const std::size_t N = config_.gridSize;

    for (std::size_t r = beginRow; r < endRow; ++r) {
        for (std::size_t c = 0; c < N; ++c) {

            const std::size_t id = r * N + c;

            const Cell& old = current_[id];
            Cell updated = old;

            // Susceptible -> Infected
            if (old.state == State::S) {
                unsigned infectedNeighbours = 0;

                const std::size_t lowR =
                    (r == 0) ? 0 : r - 1;
                const std::size_t highR =
                    std::min(r + 1, N - 1);

                const std::size_t lowC =
                    (c == 0) ? 0 : c - 1;
                const std::size_t highC =
                    std::min(c + 1, N - 1);

                // Check eight surrounding cells.
                for (std::size_t nr = lowR; nr <= highR; ++nr) {
                    for (std::size_t nc = lowC; nc <= highC; ++nc) {
                        if ((nr != r || nc != c) &&
                            current_[nr * N + nc].state ==
                                State::I) {
                            ++infectedNeighbours;
                        }
                    }
                }

                if (infectedNeighbours > 0 &&
                    rand01(config_.seed, day, id, 0) <
                        probabilities[infectedNeighbours]) {

                    updated.state = State::I;
                    updated.daysInfected = 0;
                }
            }

            // Infected -> Infected / Recovered / Dead
            else if (old.state == State::I) {
                const unsigned newDays =
                    static_cast<unsigned>(
                        old.daysInfected
                    ) + 1;

                if (newDays >= config_.infectiousDays) {
                    if (rand01(config_.seed, day, id, 1) <
                        config_.mortality) {
                        updated.state = State::D;
                    } else {
                        updated.state = State::R;
                    }

                    updated.daysInfected = 0;
                } else {
                    updated.daysInfected =
                        static_cast<std::uint16_t>(newDays);
                }
            }

            // R, V and D remain unchanged.
            next_[id] = updated;

            // Local count of next-day states.
            totals.add(updated.state);
        }
    }

    return totals;
}

// FNV-1a checksum of the complete grid.
std::uint64_t Simulation::checksum() const {
    std::uint64_t hash =
        14695981039346656037ULL;

    constexpr std::uint64_t prime =
        1099511628211ULL;

    for (const Cell& cell : current_) {
        hash = (
            hash ^ static_cast<std::uint8_t>(cell.state)
        ) * prime;

        hash = (
            hash ^ static_cast<std::uint8_t>(
                cell.daysInfected
            )
        ) * prime;

        hash = (
            hash ^ static_cast<std::uint8_t>(
                cell.daysInfected >> 8
            )
        ) * prime;
    }

    return hash;
}

// Decide whether lockdown should be active
// for the following day.
void Simulation::updateLockdown(
    const Counts& totals
) {
    const double infectedFraction =
        static_cast<double>(totals.I) /
        static_cast<double>(totals.total());

    if (!lockdownActive_ &&
        infectedFraction > config_.lockdownOn) {
        lockdownActive_ = true;
    }
    else if (lockdownActive_ &&
             infectedFraction < config_.lockdownOff) {
        lockdownActive_ = false;
    }
}

double Simulation::effectiveBeta() const {
    return config_.beta *
           config_.maskFactor *
           (lockdownActive_
                ? config_.lockdownFactor
                : 1.0);
}

// Blocking PPM snapshot.
// Async implementation will come in Part C.
void Simulation::saveSnapshot(unsigned day) const {
    fs::create_directories(config_.snapshotDir);

    const fs::path path =
        fs::path(config_.snapshotDir) /
        ("day_" + std::to_string(day) + ".ppm");

    if (config_.ioLatencyMs > 0) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(
                config_.ioLatencyMs
            )
        );
    }

    std::ofstream out(path, std::ios::binary);

    if (!out)
        throw std::runtime_error(
            "Cannot open snapshot: " + path.string()
        );

    out << "P6\n"
        << config_.gridSize << ' '
        << config_.gridSize
        << "\n255\n";

    for (const Cell& cell : current_) {
        const char* rgb;

        switch (cell.state) {
            case State::S:
                rgb = "\xD0\xD7\xDE";
                break;

            case State::I:
                rgb = "\xE8\x4A\x4A";
                break;

            case State::R:
                rgb = "\x4C\xB7\x71";
                break;

            case State::V:
                rgb = "\x53\x8B\xEC";
                break;

            case State::D:
                rgb = "\x38\x38\x38";
                break;

            default:
                throw std::logic_error("Unknown state");
        }

        out.write(rgb, 3);
    }

    if (!out)
        throw std::runtime_error(
            "Snapshot write failed: " + path.string()
        );
}

// MAIN SEQUENTIAL SIMULATION
void Simulation::run() {
    initialize();

    const fs::path csvPath(config_.csvPath);

    if (csvPath.has_parent_path()) {
        fs::create_directories(
            csvPath.parent_path()
        );
    }

    std::ofstream csv(csvPath);

    if (!csv)
        throw std::runtime_error(
            "Cannot create CSV file"
        );

    csv << "day,S,I,R,V,D,beta_eff,checksum\n";
    csv << std::setprecision(17);

    // Write one day to CSV.
    auto writeDay = [&](
        unsigned day,
        const Counts& totals,
        double beta
    ) {
        csv << day << ','
            << totals.S << ','
            << totals.I << ','
            << totals.R << ','
            << totals.V << ','
            << totals.D << ','
            << beta << ','
            << std::hex
            << std::setw(16)
            << std::setfill('0')
            << checksum()
            << std::dec
            << std::setfill(' ')
            << '\n';

        if (!csv)
            throw std::runtime_error(
                "CSV write failed"
            );
    };

    // Optional blocking snapshots.
    auto snapshotIfScheduled = [&](unsigned day) {
        if (config_.snapshotEvery > 0 &&
            config_.snapshotMode == "blocking" &&
            day % config_.snapshotEvery == 0) {
            saveSnapshot(day);
        }
    };

    Counts totals = countCurrent();

    // Day 0: initial state.
    writeDay(
        0,
        totals,
        config_.beta * config_.maskFactor
    );

    snapshotIfScheduled(0);

    updateLockdown(totals);

    unsigned completedDays = 0;

    for (unsigned day = 1;
         day <= config_.maxDays;
         ++day) {

        // Stop if no infected people remain.
        if (!config_.fixedDays && totals.I == 0)
            break;

        const double beta = effectiveBeta();

        // Precompute infection probabilities.
        // P(k) = 1 - (1 - beta)^k.
        std::array<double, 9> probabilities{};

        double survival = 1.0;

        for (std::size_t k = 1;
             k < probabilities.size();
             ++k) {

            survival *= (1.0 - beta);
            probabilities[k] = 1.0 - survival;
        }

        // Sequentially update all rows.
        totals = updateRows(
            0,
            config_.gridSize,
            day,
            probabilities
        );

        // Finish one day before swapping.
        current_.swap(next_);

        // Save the resulting population.
        writeDay(day, totals, beta);

        snapshotIfScheduled(day);

        // Decisions affect the next day.
        updateLockdown(totals);

        completedDays = day;
    }

    csv.close();

    if (!csv)
        throw std::runtime_error(
            "CSV close failed"
        );

    std::cout
        << "Completed " << completedDays << " days\n"
        << "S=" << totals.S
        << " I=" << totals.I
        << " R=" << totals.R
        << " V=" << totals.V
        << " D=" << totals.D
        << '\n'
        << "CSV: " << config_.csvPath
        << '\n';
}
