#include <iostream>
#include <cstring>
#include "merge_fit.h"

int main(int argc, char *argv[])
{
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

    mf.run(cad_in, cad_out, config_file);
    return 0;
}
