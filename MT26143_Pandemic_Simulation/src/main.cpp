
#include "Config.h"
#include "Simulation.h"

#include <exception>
#include <iostream>

int main(int argc, char* argv[]) {
    try {
        Config config = parseArgs(argc, argv);

        Simulation simulation(config);
        simulation.run();

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: "
                  << e.what()
                  << '\n';

        return 1;
    }
}
