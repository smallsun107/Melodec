// rencm-gui - command line entry point.
//
//   rencm-gui                      open the ImGui window
//   rencm-gui --gui                same as above
//   rencm-gui <file-or-dir> [out]  headless decrypt
//   rencm-gui --autotest <file>    open window, decrypt twice, exit (self-test)

#include <cstring>

#include "headless.hpp"
#include "ui.hpp"

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--fonttest") == 0) return rencm::app::run_fonttest();
    if (argc >= 3 && std::strcmp(argv[1], "--playtest") == 0)
        return rencm::app::run_playtest(argv[2]);
    if (argc >= 3 && std::strcmp(argv[1], "--covercheck") == 0)
        return rencm::app::run_covercheck(argv[2]);
    if (argc >= 3 && std::strcmp(argv[1], "--autotest") == 0)
        return rencm::app::run_gui(true, argv[2]);
    if (argc >= 2 && std::strcmp(argv[1], "--gui") != 0)
        return rencm::app::run_headless(argc, argv);
    return rencm::app::run_gui();
}
