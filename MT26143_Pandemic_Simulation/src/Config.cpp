
#include "Config.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

std::string helpText() {
    return R"(Pandemic simulator - Part A (sequential)

Usage: pandemic_sim [options]

  --grid N                 Grid size (default 1000)
  --days N                 Maximum days (default 200)
  --seed N                 Random seed (default 42)
  --beta X                 Infection probability (default 0.08)
  --infectious-days N      Infectious duration (default 7)
  --mortality X            Mortality probability (default 0.01)
  --initial-infected N     Initially infected (default 10)
  --vaccinated-fraction X  Vaccinated fraction (default 0)
  --mask-factor X          Mask multiplier (default 1)
  --lockdown-factor X      Lockdown multiplier (default 0.3)
  --lockdown-on X          Lockdown trigger (default 0.02)
  --lockdown-off X         Lockdown release (default 0.005)

  --threads N              Reserved for Part B
  --chunks N               Reserved for Part B
  --partition TYPE         strips/blocks/interleaved

  --snapshot-every K       PPM snapshot interval (0=off)
  --snapshot-mode MODE     blocking/off (async in Part C)
  --snapshot-dir DIR       Snapshot output directory
  --max-pending-writes Q   Reserved for Part C
  --io-latency MS          Simulated I/O delay

  --csv FILE               CSV output path
  --fixed-days             Disable early termination
  --help                   Show this message
)";
}

std::uint64_t parseUInt(
    const std::string& value,
    const std::string& opt
) {
    if (value.empty() || value[0] == '-')
        throw std::invalid_argument(
            "Invalid nonnegative integer for " + opt
        );

    std::size_t used = 0;
    const auto number = std::stoull(value, &used, 10);

    if (used != value.size())
        throw std::invalid_argument(
            "Invalid integer for " + opt
        );

    return static_cast<std::uint64_t>(number);
}

unsigned parseUnsigned(
    const std::string& value,
    const std::string& opt
) {
    auto n = parseUInt(value, opt);

    if (n > std::numeric_limits<unsigned>::max())
        throw std::out_of_range("Value too large for " + opt);

    return static_cast<unsigned>(n);
}

std::size_t parseSize(
    const std::string& value,
    const std::string& opt
) {
    auto n = parseUInt(value, opt);

    if (n > std::numeric_limits<std::size_t>::max())
        throw std::out_of_range("Value too large for " + opt);

    return static_cast<std::size_t>(n);
}

double parseDouble(
    const std::string& value,
    const std::string& opt
) {
    std::size_t used = 0;
    double number = std::stod(value, &used);

    if (used != value.size() || !std::isfinite(number))
        throw std::invalid_argument(
            "Invalid floating point value for " + opt
        );

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

        if (i + 1 >= argc)
            throw std::invalid_argument(
                "Missing value after " + option
            );

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

        else
            throw std::invalid_argument(
                "Unknown option: " + option
            );
    }

    if (cfg.gridSize == 0 ||
        cfg.gridSize >
            std::numeric_limits<std::size_t>::max() /
            cfg.gridSize)
        throw std::invalid_argument("Invalid grid size");

    if (cfg.infectiousDays == 0 ||
        cfg.infectiousDays > 65535)
        throw std::invalid_argument(
            "Invalid infectious duration"
        );

    if (cfg.threads == 0 ||
        cfg.chunks == 0 ||
        cfg.maxPendingWrites == 0)
        throw std::invalid_argument(
            "Thread, chunk and pending counts must be positive"
        );

    checkRange("--beta", cfg.beta, 0, 1, true);
    checkRange("--mortality", cfg.mortality, 0, 1, true);
    checkRange("--vaccinated-fraction",
               cfg.vaccinatedFraction, 0, 1, true);
    checkRange("--mask-factor",
               cfg.maskFactor, 0, 1, false);
    checkRange("--lockdown-factor",
               cfg.lockdownFactor, 0, 1, false);
    checkRange("--lockdown-on",
               cfg.lockdownOn, 0, 1, true);
    checkRange("--lockdown-off",
               cfg.lockdownOff, 0, 1, true);

    if (cfg.lockdownOff > cfg.lockdownOn)
        throw std::invalid_argument(
            "Lockdown off threshold exceeds on threshold"
        );

    if (cfg.partition != "strips" &&
        cfg.partition != "blocks" &&
        cfg.partition != "interleaved")
        throw std::invalid_argument(
            "Invalid partition type"
        );

    if (cfg.snapshotMode != "blocking" &&
        cfg.snapshotMode != "async" &&
        cfg.snapshotMode != "off")
        throw std::invalid_argument(
            "Invalid snapshot mode"
        );

    if (cfg.snapshotEvery > 0 &&
        cfg.snapshotMode == "async")
        throw std::invalid_argument(
            "Async snapshots belong to Part C"
        );

    if (cfg.csvPath.empty() || cfg.snapshotDir.empty())
        throw std::invalid_argument("Empty output path");

    const std::size_t cells =
        cfg.gridSize * cfg.gridSize;

    const std::size_t vaccinated =
        static_cast<std::size_t>(
            std::floor(
                static_cast<long double>(cells) *
                cfg.vaccinatedFraction + 0.5L
            )
        );

    if (cfg.initiallyInfected > cells - vaccinated)
        throw std::invalid_argument(
            "Initial infections exceed susceptible population"
        );

    return cfg;
}
