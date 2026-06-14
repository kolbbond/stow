// Command-line interface parser
#pragma once

#include <getopt.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "config.hpp"

namespace stow {

class CLI {
public:
    Config config;
    bool show_help = false;
    bool show_version = false;
    std::string error;

    CLI() {
        config = Config::defaults();
    }

    static void print_usage(const char* prog) {
        std::fprintf(stderr, R"(Usage: %s [OPTIONS] <command> [args...]

Position:
  -a, --anchor <pos>    Anchor position: top-left, top-right, bottom-left,
                        bottom-right, top, bottom, left, right, center
  -m, --monitor <n>     Monitor index (-1 for primary, default: -1)
  -x <pos>              X position (10, -10, 50%%, +10%%)
  -y <pos>              Y position (10, -10, 50%%, +10%%)
  --margin <px>         Margin from edge for anchors (default: 10)

Appearance:
  -f, --foreground <c>  Foreground color (#rrggbb, default: #00fbbb)
  -b, --background <c>  Background color (#rrggbb, default: #0000ff)
  -F, --font <font>     Xft font spec (default: monospace:size=10)
  -B, --border <px>     Border width (default: 2)
  -A, --alpha <0-1>     Transparency (default: 0.8)
  --align <l|r|c>       Text alignment: l=left, r=right, c=center

Behavior:
  -o, --overlay         Overlay mode - transparent, click-through (default)
  -n, --normal          Normal window mode - opaque, receives input
  -p, --period <sec>    Refresh period in seconds (default: 1)
  --on-top              Keep window on top (default)
  --below               Keep window below other windows

Info:
  -V, --version         Show version
  -h, --help            Show this help

Examples:
  %s --anchor top-right htop
  %s --monitor 1 --anchor center "watch date"
  %s -A 0.5 -f "#ff0000" --period 2 sensors

)", prog, prog, prog, prog);
    }

    static void print_version() {
        std::printf("stow 2.0.0\n");
    }

    // Parse command line arguments, returns true on success
    bool parse(int argc, char* argv[]) {
        static struct option long_options[] = {
            {"anchor",     required_argument, nullptr, 'a'},
            {"monitor",    required_argument, nullptr, 'm'},
            {"foreground", required_argument, nullptr, 'f'},
            {"background", required_argument, nullptr, 'b'},
            {"font",       required_argument, nullptr, 'F'},
            {"border",     required_argument, nullptr, 'B'},
            {"alpha",      required_argument, nullptr, 'A'},
            {"overlay",    no_argument,       nullptr, 'o'},
            {"normal",     no_argument,       nullptr, 'n'},
            {"period",     required_argument, nullptr, 'p'},
            {"version",    no_argument,       nullptr, 'V'},
            {"help",       no_argument,       nullptr, 'h'},
            {"margin",     required_argument, nullptr, 1001},
            {"align",      required_argument, nullptr, 1002},
            {"on-top",     no_argument,       nullptr, 1003},
            {"below",      no_argument,       nullptr, 1004},
            {nullptr,      0,                 nullptr, 0}
        };

        int opt;
        int option_index = 0;

        // Reset getopt
        optind = 1;

        while ((opt = getopt_long(argc, argv, "a:m:x:y:f:b:F:B:A:onp:Vh",
                                  long_options, &option_index)) != -1) {
            switch (opt) {
                case 'a':  // anchor
                    config.window.anchor = anchor_from_string(optarg);
                    break;

                case 'm':  // monitor
                    config.window.monitor = std::atoi(optarg);
                    break;

                case 'x':  // x position
                    config.window.px = Position::parse(optarg);
                    config.window.anchor = Anchor::Custom;
                    break;

                case 'y':  // y position
                    config.window.py = Position::parse(optarg);
                    config.window.anchor = Anchor::Custom;
                    break;

                case 'f':  // foreground color
                    config.window.fg = optarg;
                    break;

                case 'b':  // background color
                    config.window.bg = optarg;
                    break;

                case 'F':  // font
                    config.window.font = optarg;
                    break;

                case 'B':  // border
                    config.window.border_px = std::atoi(optarg);
                    break;

                case 'A':  // alpha
                    config.window.alpha = std::atof(optarg);
                    if (config.window.alpha < 0.0) config.window.alpha = 0.0;
                    if (config.window.alpha > 1.0) config.window.alpha = 1.0;
                    break;

                case 'o':  // overlay mode
                    config.window.overlay = true;
                    break;

                case 'n':  // normal mode
                    config.window.overlay = false;
                    break;

                case 'p':  // period
                    config.process.period = std::atoi(optarg);
                    if (config.process.period < 1) config.process.period = 1;
                    break;

                case 'V':  // version
                    show_version = true;
                    return true;

                case 'h':  // help
                    show_help = true;
                    return true;

                case 1001:  // margin
                    config.window.margin_x = std::atoi(optarg);
                    config.window.margin_y = config.window.margin_x;
                    break;

                case 1002:  // align
                    if (optarg[0] == 'l' || optarg[0] == 'r' || optarg[0] == 'c') {
                        config.window.align = optarg[0];
                    } else {
                        error = "Invalid alignment, use 'l', 'r', or 'c'";
                        return false;
                    }
                    break;

                case 1003:  // on-top
                    config.window.on_top = true;
                    break;

                case 1004:  // below
                    config.window.on_top = false;
                    break;

                case '?':
                default:
                    error = "Invalid option";
                    return false;
            }
        }

        // Remaining arguments are the command and its arguments
        if (optind < argc) {
            config.process.command = argv[optind];
            for (int i = optind + 1; i < argc; i++) {
                config.process.args.push_back(argv[i]);
            }
        }

        // Command is required unless showing help/version
        if (!show_help && !show_version && config.process.command.empty()) {
            error = "No command specified";
            return false;
        }

        return true;
    }

    // Get the command to run
    const std::string& command() const {
        return config.process.command;
    }

    // Get command arguments
    const std::vector<std::string>& args() const {
        return config.process.args;
    }
};

}  // namespace stow
