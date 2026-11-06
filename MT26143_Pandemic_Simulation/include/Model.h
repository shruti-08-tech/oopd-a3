
#pragma once

#include <cstdint>

enum class State : std::uint8_t {
    S,  // Susceptible
    I,  // Infected
    R,  // Recovered
    V,  // Vaccinated
    D   // Dead
};

struct Cell {
    std::uint16_t daysInfected = 0;
    State state = State::S;
};

struct Counts {
    std::uint64_t S = 0;
    std::uint64_t I = 0;
    std::uint64_t R = 0;
    std::uint64_t V = 0;
    std::uint64_t D = 0;

    void add(State state) {
        switch (state) {
            case State::S: ++S; break;
            case State::I: ++I; break;
            case State::R: ++R; break;
            case State::V: ++V; break;
            case State::D: ++D; break;
        }
    }

    std::uint64_t total() const {
        return S + I + R + V + D;
    }
};
