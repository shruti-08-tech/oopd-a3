
#include "Config.h"
#include "Simulation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

struct SweepOptions {
    Config model;

    std::vector<double> betas{0.04, 0.08};
    std::vector<double> vaccinations{0.0, 0.2};

    std::vector<std::uint64_t> seeds{42, 43};

    unsigned concurrency = 4;

    // Zero means unlimited.
    double timeLimit = 0.0;

    fs::path output = "results/sweep_results.csv";
};

struct Job {
    double beta;
    double vaccination;
    std::uint64_t seed;
};

struct Completed {
    SimulationSummary summary;

    Clock::time_point finished;

    double runWallSeconds = 0.0;
};

struct Active {
    Job job;
    std::future<Completed> result;
};

std::vector<std::string> split(
    const std::string& value
) {
    std::vector<std::string> result;

    std::size_t start = 0;

    while (true) {
        const auto end = value.find(',', start);

        const auto piece = value.substr(
            start,
            end == std::string::npos
                ? end
                : end - start
        );

        if (piece.empty()) {
            throw std::invalid_argument(
                "Empty value in list"
            );
        }

        result.push_back(piece);

        if (end == std::string::npos) {
            break;
        }

        start = end + 1;
    }

    return result;
}

double number(const std::string& value) {
    std::size_t used = 0;

    double result = std::stod(value, &used);

    if (used != value.size() ||
        !std::isfinite(result)) {

        throw std::invalid_argument(
            "Invalid number: " + value
        );
    }

    return result;
}

std::uint64_t integer(
    const std::string& value
) {
    if (value.empty() || value[0] == '-') {
        throw std::invalid_argument(
            "Invalid integer: " + value
        );
    }

    std::size_t used = 0;

    auto result = std::stoull(value, &used, 10);

    if (used != value.size()) {
        throw std::invalid_argument(
            "Invalid integer: " + value
        );
    }

    return static_cast<std::uint64_t>(result);
}

// Parse sweep-specific arguments and forward
// other model settings to parseArgs().
SweepOptions parseSweep(int argc, char** argv) {
    SweepOptions opts;

    std::vector<std::string> forwarded{argv[0]};

    for (int i = 1; i < argc; ++i) {
        const std::string flag(argv[i]);

        if (flag == "--help" || flag == "-h") {
            std::cout << R"(Part C2 Parameter Sweep

Usage: pandemic_sweep [options]

  --betas LIST          e.g. 0.03,0.06,0.09
  --vaccinations LIST   e.g. 0,0.25,0.5
  --seeds LIST          e.g. 41,42,43
  --concurrent-runs R   Parallel runs
  --time-limit SECONDS  Deadline for new jobs
  --output FILE         Results CSV

Other pandemic_sim model options are supported.
)";
            std::exit(EXIT_SUCCESS);
        }

        if (flag == "--fixed-days" ||
            flag == "--no-csv") {

            forwarded.push_back(flag);
            continue;
        }

        if (i + 1 >= argc) {
            throw std::invalid_argument(
                "Missing value for " + flag
            );
        }

        const std::string value(argv[++i]);

        if (flag == "--betas") {
            opts.betas.clear();

            for (const auto& x : split(value)) {
                double v = number(x);

                if (v < 0 || v > 1) {
                    throw std::invalid_argument(
                        "Beta must be between 0 and 1"
                    );
                }

                opts.betas.push_back(v);
            }
        }

        else if (flag == "--vaccinations") {
            opts.vaccinations.clear();

            for (const auto& x : split(value)) {
                double v = number(x);

                if (v < 0 || v > 1) {
                    throw std::invalid_argument(
                        "Vaccination must be between 0 and 1"
                    );
                }

                opts.vaccinations.push_back(v);
            }
        }

        else if (flag == "--seeds") {
            opts.seeds.clear();

            for (const auto& x : split(value)) {
                opts.seeds.push_back(integer(x));
            }
        }

        else if (flag == "--concurrent-runs") {
            auto n = integer(value);

            if (n == 0 ||
                n > std::numeric_limits<unsigned>::max()) {

                throw std::invalid_argument(
                    "Concurrent runs must be positive"
                );
            }

            opts.concurrency =
                static_cast<unsigned>(n);
        }

        else if (flag == "--time-limit") {
            opts.timeLimit = number(value);

            if (opts.timeLimit < 0) {
                throw std::invalid_argument(
                    "Invalid time limit"
                );
            }
        }

        else if (flag == "--output") {
            if (value.empty()) {
                throw std::invalid_argument(
                    "Empty output path"
                );
            }

            opts.output = value;
        }

        else {
            forwarded.push_back(flag);
            forwarded.push_back(value);
        }
    }

    std::vector<char*> raw;

    for (auto& arg : forwarded) {
        raw.push_back(arg.data());
    }

    opts.model = parseArgs(
        static_cast<int>(raw.size()),
        raw.data()
    );

    return opts;
}

void writeFailed(
    std::ostream& out,
    const Job& job,
    const std::string& why
) {
    std::string quoted;

    for (char c : why) {
        if (c == '"') {
            quoted += "\"\"";
        }
        else {
            quoted += c;
        }
    }

    out << std::setprecision(17)
        << job.beta << ','
        << job.vaccination << ','
        << job.seed
        << ",\"" << quoted << "\"\n";
}

int executeSweep(const SweepOptions& opts) {
    std::vector<Job> jobs;

    // Create all combinations of beta,
    // vaccination fraction, and seed.
    for (double beta : opts.betas) {
        for (double vaccination : opts.vaccinations) {

            const std::size_t cells =
                opts.model.gridSize *
                opts.model.gridSize;

            const auto vaccinated =
                static_cast<std::size_t>(
                    std::floor(
                        static_cast<long double>(cells) *
                        vaccination + 0.5L
                    )
                );

            if (opts.model.initiallyInfected >
                cells - vaccinated) {

                throw std::invalid_argument(
                    "Too many initial infections"
                );
            }

            for (auto seed : opts.seeds) {
                jobs.push_back({
                    beta,
                    vaccination,
                    seed
                });
            }
        }
    }

    if (jobs.empty()) {
        throw std::invalid_argument(
            "No sweep jobs"
        );
    }

    if (opts.output.has_parent_path()) {
        fs::create_directories(
            opts.output.parent_path()
        );
    }

    const fs::path missingPath =
        opts.output.parent_path() /
        (
            opts.output.stem().string() +
            "_missing.csv"
        );

    std::ofstream results(opts.output);
    std::ofstream missing(missingPath);

    if (!results || !missing) {
        throw std::runtime_error(
            "Cannot open sweep output CSVs"
        );
    }

    results <<
        "beta,vaccinated_fraction,seed,"
        "peak_infected_fraction,peak_day,"
        "total_infected,total_deaths,days,"
        "wall_seconds\n";

    missing <<
        "beta,vaccinated_fraction,seed,reason\n";

    results << std::setprecision(17);

    const auto start = Clock::now();

    const bool bounded = opts.timeLimit > 0;

    const auto deadline =
        start +
        std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(
                opts.timeLimit
            )
        );

    std::vector<Active> active;

    std::size_t next = 0;
    std::size_t successes = 0;
    std::size_t failures = 0;

    bool limitReached = false;

    while (next < jobs.size() || !active.empty()) {

        if (bounded && Clock::now() >= deadline) {
            limitReached = true;
        }

        // Launch jobs while capacity remains.
        while (!limitReached &&
               next < jobs.size() &&
               active.size() < opts.concurrency) {

            if (bounded &&
                Clock::now() >= deadline) {

                limitReached = true;
                break;
            }

            const Job job = jobs[next++];

            Config cfg = opts.model;

            cfg.beta = job.beta;
            cfg.vaccinatedFraction =
                job.vaccination;
            cfg.seed = job.seed;

            // Each async task owns its simulation.
            auto task = std::async(
                std::launch::async,
                [cfg]() {
                    const auto begin = Clock::now();

                    Simulation sim(cfg);

                    const auto summary =
                        sim.runSummary();

                    const auto finished =
                        Clock::now();

                    return Completed{
                        summary,
                        finished,
                        std::chrono::duration<double>(
                            finished - begin
                        ).count()
                    };
                }
            );

            active.push_back({
                job,
                std::move(task)
            });
        }

        if (active.empty()) {
            break;
        }

        struct Ready {
            std::size_t index;
            Clock::time_point finished;
            bool ok = false;
            Completed completed{};
            std::string error;
        };

        std::vector<Ready> ready;

        // Check all jobs instead of waiting only
        // for the first submitted simulation.
        for (std::size_t i = 0;
             i < active.size();
             ++i) {

            if (active[i].result.wait_for(
                    std::chrono::seconds(0)
                ) == std::future_status::ready) {

                Ready item;
                item.index = i;

                try {
                    item.completed =
                        active[i].result.get();

                    item.finished =
                        item.completed.finished;

                    item.ok = true;
                }

                catch (const std::exception& e) {
                    item.error = e.what();
                    item.finished = Clock::now();
                }

                catch (...) {
                    item.error = "unknown error";
                    item.finished = Clock::now();
                }

                ready.push_back(
                    std::move(item)
                );
            }
        }

        if (ready.empty()) {
            // Short timed wait, then check all again.
            active.front().result.wait_for(
                std::chrono::milliseconds(10)
            );
            continue;
        }

        // Handle completed jobs in finish order.
        std::sort(
            ready.begin(),
            ready.end(),
            [](const Ready& a, const Ready& b) {
                return a.finished < b.finished;
            }
        );

        for (const auto& item : ready) {
            const Job& job =
                active[item.index].job;

            if (item.ok) {
                const auto& summary =
                    item.completed.summary;

                results
                    << job.beta << ','
                    << job.vaccination << ','
                    << job.seed << ','
                    << summary.peakInfectedFraction << ','
                    << summary.peakDay << ','
                    << summary.totalInfected << ','
                    << summary.totalDeaths << ','
                    << summary.days << ','
                    << item.completed.runWallSeconds
                    << '\n';

                results.flush();
                ++successes;
            }

            else {
                writeFailed(
                    missing,
                    job,
                    "failed: " + item.error
                );

                missing.flush();
                ++failures;
            }

            std::cout
                << (successes + failures)
                << '/' << jobs.size()
                << " finished (beta=" << job.beta
                << ", v=" << job.vaccination
                << ", seed=" << job.seed
                << ")"
                << (item.ok ? "\n" : " FAILED\n");
        }

        // Remove completed futures in reverse
        // index order to preserve remaining indices.
        std::vector<std::size_t> indices;

        for (const auto& item : ready) {
            indices.push_back(item.index);
        }

        std::sort(
            indices.rbegin(),
            indices.rend()
        );

        for (auto index : indices) {
            active.erase(
                active.begin() +
                static_cast<std::ptrdiff_t>(index)
            );
        }
    }

    // Record jobs not started before the deadline.
    const std::size_t cancelled =
        jobs.size() - next;

    for (; next < jobs.size(); ++next) {
        writeFailed(
            missing,
            jobs[next],
            "not_started_deadline"
        );
    }

    if (!results || !missing) {
        throw std::runtime_error(
            "Failed writing sweep results"
        );
    }

    std::cout
        << "Sweep completed: "
        << successes << " successful, "
        << failures << " failed, "
        << cancelled
        << " not started after deadline.\n"
        << "Results: "
        << opts.output.string()
        << '\n'
        << "Missing: "
        << missingPath.string()
        << '\n';

    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto opts =
            parseSweep(argc, argv);

        return executeSweep(opts);
    }

    catch (const std::exception& e) {
        std::cerr
            << "Sweep error: "
            << e.what()
            << '\n';

        return 1;
    }
}
