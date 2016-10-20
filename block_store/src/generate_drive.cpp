#include <iostream>
#include <fstream>
#include <random>
#include <cstdint>
#include <new>
#include <cstring>
#include <chrono>

int main(int argc, char *argv[]) {
    const bool valid_args =
        (argc == 2 || (argc == 3 && (argv[1][0] == 'e' || argv[1][0] == 'r' || argv[1][0] == 'f')));
    if (valid_args) {
        try {
            std::ofstream out;
            // Not in the mood to error check every operation, just make it throw an exception.
            // It's nice to pretend errors don't happen, even if it's just for a few lines.
            out.exceptions(std::ios_base::failbit | std::ios_base::badbit);
            out.open(argc == 2 ? argv[1] : argv[2], std::ios_base::trunc);

            uint8_t *data = new uint8_t[8192];
            data[0] = 0xFF; // mark first used
            if (argc == 3) {
                if (argv[1][0] == 'r') {
                    std::mt19937 rand_eng(std::chrono::system_clock::now().time_since_epoch().count());
                    std::uniform_int_distribution<uint8_t> rand_distrib(0x00, 0xFF);
                    for (int i = 1; i < 8192; ++i) {
                        data[i] = (uint8_t) rand_distrib(rand_eng);
                    }
                } else if (argv[1][0] == 'f') {
                    memset(data + 1, 0xFF, 8191);
                }
            }
            out.write((char *)data, 8192); // I can't believe this has to be cast. Clang is just being petty.

            // Cool. FBM is done.

            for (int i = 8; i < 65536; ++i) {
                // other blocks are filled with their blockid number as a 16-bit uint
                // since it's 2 bytes, we can't use memset >:C

                // We're just going to ignore the other 7K because I'm lazy
                for (int x = 0; x < 512; ++x) { // terribly vague variables. Do as I say, not as I do.
                    ((uint16_t *)data)[x] = i;
                }
                out.write((char *)data, 1024);
            }
            delete[] data;
            out.close();
        } catch (std::exception e) {
            std::cerr << "Generation failed because: " << e.what() << std::endl;
            return -1;
        }
    } else {
        std::cout << "USEAGE:\n\tGenerate new drive file:\n\t\t"
                  << argv[0] << " FILENAME"
                  "\n\tGenerate new drive with (e)mpty, (r)andom, or (f)ull block map:\n\t\t"
                  << argv[0] << " [erf] FILENAME" << std::endl;
        return -1;
    }
    return 0;
}