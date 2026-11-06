
#include "SnapshotWriter.h"

#include <chrono>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace fs = std::filesystem;

SnapshotWriter::SnapshotWriter(const Config& cfg)
    : n_(cfg.gridSize),
      latencyMs_(cfg.ioLatencyMs),
      maxPending_(cfg.maxPendingWrites),
      mode_(cfg.snapshotMode),
      directory_(cfg.snapshotDir) {}

// Write a P6 binary PPM image.
void SnapshotWriter::writePpm(
    fs::path path,
    std::size_t n,
    unsigned latencyMs,
    const std::vector<Cell>& grid
) {
    // Simulate slow storage.
    if (latencyMs != 0) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(latencyMs)
        );
    }

    std::ofstream out(path, std::ios::binary);

    if (!out) {
        throw std::runtime_error(
            "Cannot open snapshot: " + path.string()
        );
    }

    out << "P6\n"
        << n << ' ' << n
        << "\n255\n";

    for (const Cell& cell : grid) {
        const char* rgb = nullptr;

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
        }

        if (!rgb) {
            throw std::logic_error(
                "Unexpected disease state"
            );
        }

        out.write(rgb, 3);
    }

    out.close();

    if (!out) {
        throw std::runtime_error(
            "Snapshot write failed: " + path.string()
        );
    }
}

// Submit one snapshot.
void SnapshotWriter::submit(
    unsigned day,
    const std::vector<Cell>& grid
) {
    if (mode_ == "off") {
        return;
    }

    fs::create_directories(directory_);

    const fs::path path =
        directory_ /
        ("day_" + std::to_string(day) + ".ppm");

    if (mode_ == "blocking") {
        // Main thread waits for this write.
        writePpm(path, n_, latencyMs_, grid);
        return;
    }

    if (mode_ != "async") {
        throw std::logic_error(
            "Unknown snapshot mode"
        );
    }

    // Do not exceed the maximum number Q
    // of outstanding asynchronous tasks.
    if (pending_.size() >= maxPending_) {
        auto oldest = std::move(pending_.front());

        pending_.pop_front();

        // Wait and propagate any write error.
        oldest.get();
    }

    // A deep copy is necessary because the
    // simulation continues modifying its grids.
    std::vector<Cell> snapshot = grid;

    pending_.push_back(
        std::async(
            std::launch::async,
            [
                path,
                n = n_,
                latency = latencyMs_,
                snapshot = std::move(snapshot)
            ]() {
                writePpm(
                    path,
                    n,
                    latency,
                    snapshot
                );
            }
        )
    );
}

// Wait until all background tasks complete.
void SnapshotWriter::finish() {
    std::exception_ptr firstError;

    while (!pending_.empty()) {
        auto oldest = std::move(pending_.front());
        pending_.pop_front();

        try {
            oldest.get();
        }
        catch (...) {
            if (!firstError) {
                firstError = std::current_exception();
            }
        }
    }

    if (firstError) {
        std::rethrow_exception(firstError);
    }
}
