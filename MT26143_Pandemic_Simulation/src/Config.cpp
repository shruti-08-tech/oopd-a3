
#include "Config.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

std::string helpText() {
    return R"(Pandemic Simulator - Parts A+B+C

Usage: pandemic_sim [options]

  --grid N                 Grid size
  --days N                 Maximum days
  --seed N                 Random seed
  --beta X                 Transmission probability
  --infectious-days N      Infectious duration
  --mortality X            Mortality probability
  --initial-infected N     Initially infected
  --vaccinated-fraction X  Vaccinated fraction

  --mask-factor X          Mask multiplier
  --lockdown-factor X      Lockdown multiplier
  --lockdown-on X          Lockdown activation
  --lockdown-off X         Lockdown release

  --mode MODE              sequential/spawn/pool
  --threads T              Worker threads
  --chunks C               Work chunks
  --partition TYPE         strips/blocks/interleaved

  --snapshot-every K       Snapshot interval
  --snapshot-mode MODE     blocking/async/off
  --snapshot-dir DIR       Image output directory
  --max-pending-writes Q   Maximum pending writes
  --io-latency MS          Artificial I/O latency

  --csv FILE               Daily CSV filename
  --no-csv                 Disable daily CSV output
  --fixed-days             Disable early termination
  --help                   Print help
)";
}

std::uint64_t parseUInt(
    const std::string& value,
    const std::string& option
) {
    if (value.empty() || value[0] == '-') {
        throw std::invalid_argument(
            "Invalid integer for " + option
        );
    }

    std::size_t used = 0;
    auto number = std::stoull(value, &used, 10);

    if (used != value.size()) {
        throw std::invalid_argument(
            "Invalid integer for " + option
        );
    }

    return static_cast<std::uint64_t>(number);
}

unsigned parseUnsigned(
    const std::string& value,
    const std::string& option
) {
    auto number = parseUInt(value, option);

    if (number > std::numeric_limits<unsigned>::max()) {
        throw std::out_of_range(
            "Value too large for " + option
        );
    }

    return static_cast<unsigned>(number);
}

std::size_t parseSize(
    const std::string& value,
    const std::string& option
) {
    auto number = parseUInt(value, option);

    if (number > std::numeric_limits<std::size_t>::max()) {
        throw std::out_of_range(
            "Value too large for " + option
        );
    }

    return static_cast<std::size_t>(number);
}

double parseDouble(
    const std::string& value,
    const std::string& option
) {
    std::size_t used = 0;
    double number = std::stod(value, &used);

    if (used != value.size() || !std::isfinite(number)) {
        throw std::invalid_argument(
            "Invalid number for " + option
        );
    }

    return number;
}

void checkRange(
    const char* name,
    double value,
    double low,
    double high,
    bool lowInclusive
) {
    if ((lowInclusive ? value < low : value <= low)
        || value > high) {
        throw std::invalid_argument(
            std::string(name) + " outside valid range"
        );
    }
}

} // namespace

Config parseArgs(int argc, char* argv[]) {
    Config cfg;

    for (int i = 1; i < argc; ++i) {
        std::string option = argv[i];

        if (option == "--help" || option == "-h") {
            std::cout << helpText();
            std::exit(EXIT_SUCCESS);
        }

        if (option == "--fixed-days") {
            cfg.fixedDays = true;
            continue;
        }

        if (option == "--no-csv") {
            cfg.writeDailyCsv = false;
            continue;
        }

        if (i + 1 >= argc) {
            throw std::invalid_argument(
                "Missing value after " + option
            );
        }

        std::string value = argv[++i];

        if (option == "--grid")
            cfg.gridSize = parseSize(value, option);

        else if (option == "--days")
            cfg.maxDays = parseUnsigned(value, option);

        else if (option == "--seed")
            cfg.seed = parseUInt(value, option);

        else if (option == "--beta")
            cfg.beta = parseDouble(value, option);

        else if (option == "--infectious-days")
            cfg.infectiousDays = parseUnsigned(value, option);

        else if (option == "--mortality")
            cfg.mortality = parseDouble(value, option);

        else if (option == "--initial-infected")
            cfg.initiallyInfected = parseSize(value, option);

        else if (option == "--vaccinated-fraction")
            cfg.vaccinatedFraction = parseDouble(value, option);

        else if (option == "--mask-factor")
            cfg.maskFactor = parseDouble(value, option);

        else if (option == "--lockdown-factor")
            cfg.lockdownFactor = parseDouble(value, option);

        else if (option == "--lockdown-on")
            cfg.lockdownOn = parseDouble(value, option);

        else if (option == "--lockdown-off")
            cfg.lockdownOff = parseDouble(value, option);

        else if (option == "--mode")
            cfg.mode = value;

        else if (option == "--threads")
            cfg.threads = parseUnsigned(value, option);

        else if (option == "--chunks")
            cfg.chunks = parseUnsigned(value, option);

        else if (option == "--partition")
            cfg.partition = value;

        else if (option == "--snapshot-every")
            cfg.snapshotEvery = parseUnsigned(value, option);

        else if (option == "--snapshot-mode")
            cfg.snapshotMode = value;

        else if (option == "--snapshot-dir")
            cfg.snapshotDir = value;

        else if (option == "--max-pending-writes")
            cfg.maxPendingWrites = parseUnsigned(value, option);

        else if (option == "--io-latency")
            cfg.ioLatencyMs = parseUnsigned(value, option);

        else if (option == "--csv")
            cfg.csvPath = value;

        else {
            throw std::invalid_argument(
                "Unknown option: " + option
            );
        }
    }

    if (cfg.gridSize == 0 ||
        cfg.gridSize >
            std::numeric_limits<std::size_t>::max() /
            cfg.gridSize) {
        throw std::invalid_argument(
            "Invalid grid size"
        );
    }

    if (cfg.infectiousDays == 0 ||
        cfg.infectiousDays > 65535) {
        throw std::invalid_argument(
            "Invalid infectious duration"
        );
    }

    if (cfg.threads == 0 ||
        cfg.chunks == 0 ||
        cfg.maxPendingWrites == 0) {
        throw std::invalid_argument(
            "Threads, chunks and pending writes must be positive"
        );
    }

    checkRange("--beta", cfg.beta, 0, 1, true);
    checkRange("--mortality", cfg.mortality, 0, 1, true);

    checkRange(
        "--vaccinated-fraction",
        cfg.vaccinatedFraction, 0, 1, true
    );

    checkRange(
        "--mask-factor",
        cfg.maskFactor, 0, 1, false
    );

    checkRange(
        "--lockdown-factor",
        cfg.lockdownFactor, 0, 1, false
    );

    checkRange(
        "--lockdown-on",
        cfg.lockdownOn, 0, 1, true
    );

    checkRange(
        "--lockdown-off",
        cfg.lockdownOff, 0, 1, true
    );

    if (cfg.lockdownOff > cfg.lockdownOn) {
        throw std::invalid_argument(
            "Invalid lockdown thresholds"
        );
    }

    if (cfg.mode != "sequential" &&
        cfg.mode != "spawn" &&
        cfg.mode != "pool") {
        throw std::invalid_argument(
            "Invalid execution mode"
        );
    }

    if (cfg.partition != "strips" &&
        cfg.partition != "blocks" &&
        cfg.partition != "interleaved") {
        throw std::invalid_argument(
            "Invalid partition"
        );
    }

    if (cfg.snapshotMode != "blocking" &&
        cfg.snapshotMode != "async" &&
        cfg.snapshotMode != "off") {
        throw std::invalid_argument(
            "Invalid snapshot mode"
        );
    }

    if (cfg.csvPath.empty() ||
        cfg.snapshotDir.empty()) {
        throw std::invalid_argument(
            "Empty output path"
        );
    }

    const std::size_t cells =
        cfg.gridSize * cfg.gridSize;

    const std::size_t vaccinated =
        static_cast<std::size_t>(
            std::floor(
                static_cast<long double>(cells) *
                cfg.vaccinatedFraction + 0.5L
            )
        );

    if (cfg.initiallyInfected > cells - vaccinated) {
        throw std::invalid_argument(
            "Initial infection count exceeds susceptible population"
        );
    }

    return cfg;
}
