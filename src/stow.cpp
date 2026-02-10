// stow - X11 text overlay window manager
#include "stow/cli.hpp"
#include "stow/config.hpp"
#include "stow/monitor.hpp"
#include "stow/layout.hpp"
#include "ptyprocess.hpp"
#include "pipeprocess.hpp"
#include "xwindow.hpp"

#include <unistd.h>

int main(int argc, char** argv) {
    // Parse command line
    stow::CLI cli;
    if (!cli.parse(argc, argv)) {
        std::fprintf(stderr, "Error: %s\n", cli.error.c_str());
        stow::CLI::print_usage(argv[0]);
        return 1;
    }

    // Handle help/version
    if (cli.show_help) {
        stow::CLI::print_usage(argv[0]);
        return 0;
    }
    if (cli.show_version) {
        stow::CLI::print_version();
        return 0;
    }

    // Create window with configuration
    ShXWindowPr xwin = XWindow::create(cli.config.window);
    xwin->setup();

    // Set up monitor if specified
    if (cli.config.window.monitor >= 0 || cli.config.window.anchor != stow::Anchor::Custom) {
        auto monitor_mgr = stow::MonitorManager::create(xwin->_dpy);
        const stow::Monitor* mon = monitor_mgr->at(cli.config.window.monitor);
        if (mon) {
            xwin->set_monitor_geometry(mon->x, mon->y, mon->width, mon->height);
        }
    }

    // Main loop
    while (true) {
        ShProcessPr process;
        if (cli.config.process.use_pty) {
            auto pty = PTYProcess::create(cli.config.process);
            pty->set_default_fg(cli.config.window.fg);
            process = pty;
        } else {
            process = PipeProcess::create(cli.config.process);
        }

        process->setup();
        process->start_cmd(cli.command(), cli.args());
        process->read_text(xwin);

        sleep(cli.config.process.period);
    }

    return 0;
}
