/* See LICENSE file for copyright and license details. */

static int borderpx = 0;
static char font[] = "monospace:size=10";

/* background opacity */
static double alpha = 0.01;

/* X window geometry */
// assume 1920x1080?
struct g px = {1200};
struct g py = {70};
struct g tx = {5};
struct g ty = {10};

/* text alignment: l, r and c for left, right and centered respectively */
static char align = 'l';

/* foreground and background colors */
//static char *colors[2] = { "#33ffd1", "#000000" };
static char *colors[2] = { "#00bbbb", "#000000" };

/* time in seconds between subcommand runs.
0 will completely disable subcommand restarts and -1 will make them instant.
in any case a click on a window will still immediately restart subcommand */
static int period = 1;

/* delimeter string, encountered as a separate line in subcommand output
signals stw to render buffered text and continue with next frame;
it is the only valid use of non-printable characters in subcommand output */
static char delimeter[] = "\4";

static bool window_on_top = 1;
