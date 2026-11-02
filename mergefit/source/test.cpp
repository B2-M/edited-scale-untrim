#include <iostream>
#include <cstring>
#include "merge_fit.h"
#include <cstdlib>
#include <exception>
#include <sys/resource.h>

// Stops a runaway run with std::bad_alloc before it exhausts the machine.
// QUADRIFLOW_MAX_MEMORY_GB overrides the default of 8; 0 disables the limit.
// AddressSanitizer reserves far more address space than this, so it is skipped there.
static void limit_address_space()
{
#ifndef __SANITIZE_ADDRESS__
    const char* env = std::getenv("QUADRIFLOW_MAX_MEMORY_GB");
    const double gb = env ? std::atof(env) : 8.0;
    if (gb <= 0) return;
    rlimit limit;
    limit.rlim_cur = limit.rlim_max = static_cast<rlim_t>(gb * 1024 * 1024 * 1024);
    setrlimit(RLIMIT_AS, &limit);
#endif
}


int main(int argc, char *argv[])
{
    limit_address_space();

    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " input.obj setting.config [output.step] [--sharp | --no-sharp]" << std::endl;
        std::cerr << "  output.step  Path to write the fitted STEP file (default: output.step)" << std::endl;
        std::cerr << "  --sharp      Enable sharp feature preservation (overrides preserve_sharp in config)" << std::endl;
        std::cerr << "  --no-sharp   Disable sharp feature preservation (overrides preserve_sharp in config)" << std::endl;
        return 1;
    }

    std::string cad_in = argv[1];
    std::string cad_out = "output.step";
    std::string config_file = argv[2];

    MergeFit mf;
    mf.set_surface_degree(3);

    // Parse optional positional output path and CLI flags (any position after
    // the first two required args)
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--sharp") == 0) {
            mf.set_preserve_sharp(1);
        } else if (std::strcmp(argv[i], "--no-sharp") == 0) {
            mf.set_preserve_sharp(0);
        } else if (argv[i][0] != '-') {
            cad_out = argv[i];
        } else {
            std::cerr << "Unknown argument: " << argv[i] << std::endl;
            return 1;
        }
    }

    try {
        mf.run(cad_in, cad_out, config_file);
    } catch (const std::exception& e) {
        // quad.vtk is written before patch extraction, so it may still be usable.
        std::cerr << "QuadriFlow failed: " << e.what() << std::endl;
        return 3;
    }
    return 0;
}
