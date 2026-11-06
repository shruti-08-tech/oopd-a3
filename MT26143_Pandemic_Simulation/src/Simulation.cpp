
#include "Simulation.h"
#include "Random.h"
#include "ThreadPool.h"
#include "SnapshotWriter.h"

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

Counts Simulation::countCurrent() const {
    Counts counts;

    for (const Cell& cell : current_) {
        counts.add(cell.state);
    }

    return counts;
}

Counts Simulation::updateChunk(
    const Chunk& chunk,
    unsigned day,
    const std::array<double, 9>& p
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
                    ) < p[infectedNeighbours]) {

                    updated.state = State::I;
                    updated.daysInfected = 0;
                }
            }

            else if (old.state == State::I) {
                const unsigned newDays =
                    static_cast<unsigned>(
                        old.daysInfected
                    ) + 1;

                if (newDays >= config_.infectiousDays) {

                    updated.state =
                        rand01(
                            config_.seed,
                            day,
                            id,
                            1
                        ) < config_.mortality
                            ? State::D
                            : State::R;

                    updated.daysInfected = 0;
                }
                else {
                    updated.daysInfected =
                        static_cast<std::uint16_t>(newDays);
                }
            }

            next_[id] = updated;
            totals.add(updated.state);
        }
    }

    return totals;
}

Counts Simulation::updateSpawn(
    const std::vector<Chunk>& chunks,
    unsigned day,
    const std::array<double, 9>& p
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
                            p
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
            if (worker.joinable()) {
                worker.join();
            }
        }
        throw;
    }

    for (auto& worker : workers) {
        worker.join();
    }

    Counts totals;

    for (std::size_t i = 0; i < local.size(); ++i) {
        if (errors[i]) {
            std::rethrow_exception(errors[i]);
        }
        totals += local[i];
    }

    return totals;
}

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

SimulationSummary Simulation::execute(
    bool publicOutput
) {
    initialize();

    std::ofstream csv;

    if (publicOutput && config_.writeDailyCsv) {
        const fs::path csvPath(config_.csvPath);

        if (csvPath.has_parent_path()) {
            fs::create_directories(
                csvPath.parent_path()
            );
        }

        csv.open(csvPath);

        if (!csv) {
            throw std::runtime_error(
                "Cannot create CSV: " + csvPath.string()
            );
        }

        csv << "day,S,I,R,V,D,beta_eff,checksum\n";
        csv << std::setprecision(17);
    }

    auto writeDay = [&](
        unsigned day,
        const Counts& totals,
        double beta
    ) {
        if (!csv.is_open()) {
            return;
        }

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

    // Part C: blocking or async snapshots.
    SnapshotWriter snapshots(config_);

    auto snapshotIfScheduled = [&](unsigned day) {
        if (publicOutput &&
            config_.snapshotEvery > 0 &&
            day % config_.snapshotEvery == 0) {

            snapshots.submit(day, current_);
        }
    };

    Counts totals = countCurrent();

    const std::uint64_t initialS = totals.S;

    SimulationSummary summary;

    summary.peakInfectedFraction =
        static_cast<double>(totals.I) /
        static_cast<double>(totals.total());

    summary.peakDay = 0;

    writeDay(
        0,
        totals,
        config_.beta * config_.maskFactor
    );

    updateLockdown(totals);

    std::vector<Chunk> chunks;

    if (config_.mode != "sequential") {
        chunks = makeChunks(
            config_.gridSize,
            config_.chunks,
            config_.partition
        );
    }

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

    // Start timing after initialization.
    const auto begin =
        std::chrono::steady_clock::now();

    try {
        snapshotIfScheduled(0);

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

            // Part A: Sequential.
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

            // Part B: Thread per chunk per day.
            else if (config_.mode == "spawn") {
                totals = updateSpawn(
                    chunks,
                    day,
                    probabilities
                );
            }

            // Part B: Persistent thread pool.
            else {
                totals = pool->execute(
                    chunks,
                    day,
                    probabilities
                );
            }

            // Safe because workers have completed.
            current_.swap(next_);

            writeDay(day, totals, beta);

            // Can overlap snapshot writing with
            // computation of the following day.
            snapshotIfScheduled(day);

            updateLockdown(totals);

            const double fraction =
                static_cast<double>(totals.I) /
                static_cast<double>(totals.total());

            if (fraction >
                summary.peakInfectedFraction) {

                summary.peakInfectedFraction = fraction;
                summary.peakDay = day;
            }

            summary.days = day;
        }

        // Wait for background snapshot tasks.
        snapshots.finish();
    }
    catch (...) {
        // Drain pending tasks even if an error occurs.
        try {
            snapshots.finish();
        }
        catch (const std::exception& e) {
            std::cerr
                << "Background snapshot failure: "
                << e.what()
                << '\n';
        }

        throw;
    }

    summary.loopSeconds =
        std::chrono::duration<double>(
            std::chrono::steady_clock::now() - begin
        ).count();

    // Initial infected + newly infected.
    summary.totalInfected =
        config_.initiallyInfected +
        initialS - totals.S;

    summary.totalDeaths = totals.D;

    if (csv.is_open()) {
        csv.close();

        if (!csv) {
            throw std::runtime_error(
                "CSV close failed"
            );
        }
    }

    if (publicOutput) {
        std::cout
            << "Completed " << summary.days
            << " days; "
            << "S=" << totals.S
            << " I=" << totals.I
            << " R=" << totals.R
            << " V=" << totals.V
            << " D=" << totals.D
            << '\n';

        if (config_.writeDailyCsv) {
            std::cout
                << "CSV: " << config_.csvPath
                << '\n';
        }

        std::cout
            << "Simulation loop seconds: "
            << std::fixed
            << std::setprecision(6)
            << summary.loopSeconds
            << '\n';
    }

    return summary;
}

void Simulation::run() {
    (void)execute(true);
}

SimulationSummary Simulation::runSummary() {
    return execute(false);
}
