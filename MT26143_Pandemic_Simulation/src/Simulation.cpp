
#include "Simulation.h"
#include "Random.h"
#include "ThreadPool.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <exception>
#include <memory>
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

// Initialize grid, vaccination and infections.
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

// Count all cells in the current grid.
Counts Simulation::countCurrent() const {
    Counts counts;

    for (const Cell& cell : current_) {
        counts.add(cell.state);
    }

    return counts;
}

// Update cells belonging to a given chunk.
// Workers read only current_ and write only their
// assigned cells in next_.
Counts Simulation::updateChunk(
    const Chunk& chunk,
    unsigned day,
    const std::array<double, 9>& probabilities
) {
    Counts totals;

    const std::size_t N = config_.gridSize;

    for (std::size_t r = chunk.rowBegin;
         r < chunk.rowEnd;
         r += chunk.rowStride) {

        for (std::size_t c = chunk.colBegin;
             c < chunk.colEnd;
             ++c) {

            const std::size_t id = r * N + c;

            const Cell& old = current_[id];
            Cell updated = old;

            // Susceptible person
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

                // Check up to eight neighbours.
                for (std::size_t nr = lowR;
                     nr <= highR;
                     ++nr) {

                    for (std::size_t nc = lowC;
                         nc <= highC;
                         ++nc) {

                        if ((nr != r || nc != c) &&
                            current_[nr * N + nc].state ==
                                State::I) {
                            ++infectedNeighbours;
                        }
                    }
                }

                if (infectedNeighbours > 0 &&
                    rand01(
                        config_.seed,
                        day,
                        id,
                        0
                    ) < probabilities[infectedNeighbours]) {

                    updated.state = State::I;
                    updated.daysInfected = 0;
                }
            }

            // Infected person
            else if (old.state == State::I) {
                const unsigned newDays =
                    static_cast<unsigned>(
                        old.daysInfected
                    ) + 1;

                if (newDays >= config_.infectiousDays) {
                    if (rand01(
                            config_.seed,
                            day,
                            id,
                            1
                        ) < config_.mortality) {

                        updated.state = State::D;
                    }
                    else {
                        updated.state = State::R;
                    }

                    updated.daysInfected = 0;
                }
                else {
                    updated.daysInfected =
                        static_cast<std::uint16_t>(newDays);
                }
            }

            // Recovered, vaccinated, dead: unchanged.
            next_[id] = updated;

            totals.add(updated.state);
        }
    }

    return totals;
}

// Thread-per-day version.
// Create one thread for each chunk and join all.
Counts Simulation::updateSpawn(
    const std::vector<Chunk>& chunks,
    unsigned day,
    const std::array<double, 9>& probabilities
) {
    std::vector<Counts> local(chunks.size());

    std::vector<std::exception_ptr>
        errors(chunks.size());

    std::vector<std::thread> workers;
    workers.reserve(chunks.size());

    try {
        for (std::size_t i = 0; i < chunks.size(); ++i) {
            workers.emplace_back(
                [&, i]() {
                    try {
                        local[i] = updateChunk(
                            chunks[i],
                            day,
                            probabilities
                        );
                    }
                    catch (...) {
                        errors[i] =
                            std::current_exception();
                    }
                }
            );
        }
    }
    catch (...) {
        for (auto& worker : workers) {
            if (worker.joinable())
                worker.join();
        }
        throw;
    }

    // All workers must finish before grid swap.
    for (auto& worker : workers) {
        worker.join();
    }

    Counts totals;

    for (std::size_t i = 0; i < local.size(); ++i) {
        if (errors[i])
            std::rethrow_exception(errors[i]);

        totals += local[i];
    }

    return totals;
}

// Whole-grid checksum: FNV-1a.
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

// Apply lockdown decision for the following day.
void Simulation::updateLockdown(
    const Counts& totals
) {
    const double fraction =
        static_cast<double>(totals.I) /
        static_cast<double>(totals.total());

    if (!lockdownActive_ &&
        fraction > config_.lockdownOn) {

        lockdownActive_ = true;
    }
    else if (lockdownActive_ &&
             fraction < config_.lockdownOff) {

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

// Blocking PPM snapshots.
// Full asynchronous snapshots will be in Part C.
void Simulation::saveSnapshot(unsigned day) const {
    fs::create_directories(config_.snapshotDir);

    const auto path =
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

    if (!out) {
        throw std::runtime_error(
            "Cannot open snapshot: " + path.string()
        );
    }

    out << "P6\n"
        << config_.gridSize << ' '
        << config_.gridSize << "\n255\n";

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
                throw std::logic_error(
                    "Unknown state"
                );
        }

        out.write(rgb, 3);
    }

    if (!out) {
        throw std::runtime_error(
            "Snapshot write failed: " + path.string()
        );
    }
}

// Main simulation entry point.
void Simulation::run() {
    initialize();

    const fs::path csvPath(config_.csvPath);

    if (csvPath.has_parent_path()) {
        fs::create_directories(
            csvPath.parent_path()
        );
    }

    std::ofstream csv(csvPath);

    if (!csv) {
        throw std::runtime_error(
            "Cannot create CSV: " + csvPath.string()
        );
    }

    csv << "day,S,I,R,V,D,beta_eff,checksum\n";
    csv << std::setprecision(17);

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

        if (!csv) {
            throw std::runtime_error(
                "CSV write failed"
            );
        }
    };

    auto snapshotIfScheduled = [&](unsigned day) {
        if (config_.snapshotEvery > 0 &&
            config_.snapshotMode == "blocking" &&
            day % config_.snapshotEvery == 0) {

            saveSnapshot(day);
        }
    };

    Counts totals = countCurrent();

    // Record initial conditions.
    writeDay(
        0,
        totals,
        config_.beta * config_.maskFactor
    );

    snapshotIfScheduled(0);
    updateLockdown(totals);

    // Create chunks only for parallel modes.
    std::vector<Chunk> chunks;

    if (config_.mode != "sequential") {
        chunks = makeChunks(
            config_.gridSize,
            config_.chunks,
            config_.partition
        );
    }

    // Persistent pool for all simulation days.
    std::unique_ptr<ThreadPool> pool;

    if (config_.mode == "pool") {
        pool = std::make_unique<ThreadPool>(
            config_.threads,

            [this](
                const Chunk& chunk,
                unsigned day,
                const std::array<double, 9>& p
            ) {
                return updateChunk(chunk, day, p);
            }
        );
    }

    unsigned completedDays = 0;

    for (unsigned day = 1;
         day <= config_.maxDays;
         ++day) {

        if (!config_.fixedDays && totals.I == 0) {
            break;
        }

        const double beta = effectiveBeta();

        std::array<double, 9> probabilities{};

        double survival = 1.0;

        for (std::size_t k = 1;
             k < probabilities.size();
             ++k) {

            survival *= (1.0 - beta);
            probabilities[k] = 1.0 - survival;
        }

        // Select execution mode.
        if (config_.mode == "sequential") {

            totals = updateChunk(
                {
                    0,
                    config_.gridSize,
                    0,
                    config_.gridSize,
                    1
                },
                day,
                probabilities
            );
        }
        else if (config_.mode == "spawn") {

            totals = updateSpawn(
                chunks,
                day,
                probabilities
            );
        }
        else {

            totals = pool->execute(
                chunks,
                day,
                probabilities
            );
        }

        // Workers are finished: safe to swap.
        current_.swap(next_);

        writeDay(day, totals, beta);
        snapshotIfScheduled(day);

        // Changes transmission for the next day.
        updateLockdown(totals);

        completedDays = day;
    }

    csv.close();

    if (!csv) {
        throw std::runtime_error(
            "CSV close failed"
        );
    }

    std::cout
        << "Completed " << completedDays << " days; "
        << "S=" << totals.S
        << " I=" << totals.I
        << " R=" << totals.R
        << " V=" << totals.V
        << " D=" << totals.D
        << '\n'
        << "CSV: " << config_.csvPath
        << '\n';
}
