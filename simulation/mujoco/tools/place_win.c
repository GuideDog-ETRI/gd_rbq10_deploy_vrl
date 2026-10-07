// Move and/or resize one X11 window by id: place_win <display> <win_id> <x> <y> [<width> <height>]
// Used by rbq_sim.sh to lay out the simulator windows: Mujoco's Xephyr host window at the top left,
// shrunk to exactly what Mujoco rendered (Mujoco renders at 2/3 of Xephyr's launch size and never
// reacts to resizes, so a plain XMoveResizeWindow removes the black border without changing what
// Mujoco renders), and the student input viewer right of it at the same height.
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc != 5 && argc != 7) {
        fprintf(stderr, "usage: %s <display> <win_id> <x> <y> [<width> <height>]\n", argv[0]);
        return 1;
    }
    Display* d = XOpenDisplay(argv[1]);
    if (!d) { fprintf(stderr, "cannot open display %s\n", argv[1]); return 1; }
    Window w = (Window)strtoul(argv[2], NULL, 0);
    const int x = atoi(argv[3]), y = atoi(argv[4]);
    if (argc == 7) XMoveResizeWindow(d, w, x, y, (unsigned)atoi(argv[5]), (unsigned)atoi(argv[6]));
    else XMoveWindow(d, w, x, y);
    XFlush(d);
    XCloseDisplay(d);
    return 0;
}
