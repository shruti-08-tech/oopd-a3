
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

// A chunk owns its output cells exclusively.
struct Chunk {
    std::size_t rowBegin = 0;
    std::size_t rowEnd = 0;
    std::size_t colBegin = 0;
    std::size_t colEnd = 0;
    std::size_t rowStride = 1;
};

inline std::size_t boundary(
    std::size_t part,
    std::size_t totalParts,
    std::size_t length
) {
    return part * (length / totalParts) +
           (part * (length % totalParts)) / totalParts;
}

inline std::vector<Chunk> makeChunks(
    std::size_t n,
    std::size_t requested,
    const std::string& partition
) {
    if (n == 0 || requested == 0)
        throw std::invalid_argument(
            "Grid and chunk count must be positive"
        );

    std::vector<Chunk> chunks;

    if (partition == "strips") {
        chunks.reserve(requested);

        for (std::size_t i = 0; i < requested; ++i) {
            chunks.push_back({
                boundary(i, requested, n),
                boundary(i + 1, requested, n),
                0, n, 1
            });
        }
    }
    else if (partition == "interleaved") {
        chunks.reserve(requested);

        for (std::size_t i = 0; i < requested; ++i) {
            chunks.push_back({
                i, n, 0, n, requested
            });
        }
    }
    else if (partition == "blocks") {
        // Build near-square, non-overlapping tiles.
        const auto count = std::min(n * n, requested);

        const auto root = static_cast<std::size_t>(
            std::sqrt(static_cast<double>(count)) + 0.5
        );

        const auto minimumBands = (count - 1) / n + 1;

        const auto bands = std::min(
            n, std::max(minimumBands, root)
        );

        chunks.reserve(requested);

        for (std::size_t band = 0;
             band < bands;
             ++band) {

            const auto rowStart =
                boundary(band, bands, n);

            const auto rowEnd =
                boundary(band + 1, bands, n);

            const auto columns =
                count / bands +
                (band < count % bands ? 1 : 0);

            for (std::size_t col = 0;
                 col < columns;
                 ++col) {

                chunks.push_back({
                    rowStart,
                    rowEnd,
                    boundary(col, columns, n),
                    boundary(col + 1, columns, n),
                    1
                });
            }
        }

        // Empty chunks when C > N*N.
        while (chunks.size() < requested) {
            chunks.push_back({
                n, n, 0, 0, 1
            });
        }
    }
    else {
        throw std::invalid_argument(
            "Unknown partition: " + partition
        );
    }

    return chunks;
}
