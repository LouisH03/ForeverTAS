#include "searches/serial_search_runner.h"

#include <exception>
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "expected Packs directory and replay path\n";
        return 2;
    }

    try {
        const forevertas::SearchResult result =
                forevertas::RunSerialSearch({
                        argv[1],
                        argv[2],
                        forevertas::DefaultSerialBruteForceSettings()});
        std::cout << "score=" << result.bestScore
                  << " attempts=" << result.requestedAttempts << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
