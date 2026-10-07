#include <X11/Xlib.h>
#include <stdlib.h>
#include <unistd.h>
extern int XTestFakeKeyEvent(Display *, unsigned int, Bool, unsigned long);
extern int XTestFakeMotionEvent(Display *, int, int, int, unsigned long);
/* xtkey <display> <window-hex> <keysym>: focus the window, move the pointer into it, press+release */
int main(int c, char **v) {
    Display *d = XOpenDisplay(v[1]); if (!d) return 1;
    Window w = strtoul(v[2], 0, 16);
    XRaiseWindow(d, w); XSetInputFocus(d, w, RevertToParent, CurrentTime);
    XTestFakeMotionEvent(d, -1, 400, 300, 0); XFlush(d); usleep(100000);
    KeyCode kc = XKeysymToKeycode(d, XStringToKeysym(v[3]));
    XTestFakeKeyEvent(d, kc, True, 0); XFlush(d); usleep(50000);
    XTestFakeKeyEvent(d, kc, False, 0); XFlush(d);
    XCloseDisplay(d); return 0;
}
