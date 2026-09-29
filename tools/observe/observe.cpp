// opense4-observe: drive and inspect another X11 application (via XWayland on
// Wayland desktops). Used to observe the original game's behavior while
// building the reimplementation - black-box only: it sends input and reads
// window pixels, nothing else.
//
//   opense4-observe list                         windows with id, geometry, name
//   opense4-observe click  <win> <x> <y> [btn]   click at window-relative coords
//   opense4-observe dclick <win> <x> <y>         double click
//   opense4-observe key    <win> <keysym>...     press keys (e.g. Return, F1, a)
//   opense4-observe type   <win> <text>          type text
//   opense4-observe move   <win> <x> <y>         move the pointer
//   opense4-observe sclick <win> <x> <y> [btn]   click with events sent straight to the
//                                            window (works where XTest is blocked,
//                                            e.g. XWayland without input permission)
//
// <win> is a window id (0x...) or a substring of the window name.

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

void pause(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

std::vector<Window> clientWindows(Display* dpy) {
    std::vector<Window> out;
    Atom prop = XInternAtom(dpy, "_NET_CLIENT_LIST", True);
    Atom type;
    int format;
    unsigned long count = 0, after = 0;
    unsigned char* data = nullptr;
    if (prop != None && XGetWindowProperty(dpy, DefaultRootWindow(dpy), prop, 0, 4096, False, XA_WINDOW, &type, &format, &count,
                                           &after, &data) == Success && data) {
        auto* ids = reinterpret_cast<Window*>(data);
        out.assign(ids, ids + count);
        XFree(data);
    }
    return out;
}

std::string windowName(Display* dpy, Window w) {
    char* name = nullptr;
    std::string out;
    if (XFetchName(dpy, w, &name) && name) {
        out = name;
        XFree(name);
    }
    return out;
}

Window findWindow(Display* dpy, const char* spec) {
    if (std::strncmp(spec, "0x", 2) == 0) return static_cast<Window>(std::strtoul(spec, nullptr, 16));
    for (Window w : clientWindows(dpy))
        if (windowName(dpy, w).find(spec) != std::string::npos) return w;
    return None;
}

void focus(Display* dpy, Window w) {
    XEvent ev{};
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 2;  // source: pager / tool
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, DefaultRootWindow(dpy), False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XRaiseWindow(dpy, w);
    XFlush(dpy);
    pause(150);
}

void moveTo(Display* dpy, Window w, int x, int y) {
    int rx = 0, ry = 0;
    Window child;
    XTranslateCoordinates(dpy, w, DefaultRootWindow(dpy), x, y, &rx, &ry, &child);
    XTestFakeMotionEvent(dpy, -1, rx, ry, CurrentTime);
    XFlush(dpy);
    pause(60);
}

void click(Display* dpy, unsigned button) {
    XTestFakeButtonEvent(dpy, button, True, CurrentTime);
    XFlush(dpy);
    pause(60);
    XTestFakeButtonEvent(dpy, button, False, CurrentTime);
    XFlush(dpy);
    pause(60);
}

void pressKey(Display* dpy, KeySym sym, bool shift) {
    const KeyCode code = XKeysymToKeycode(dpy, sym);
    const KeyCode shiftCode = XKeysymToKeycode(dpy, XK_Shift_L);
    if (!code) {
        std::fprintf(stderr, "no keycode for keysym %lu\n", sym);
        return;
    }
    if (shift) XTestFakeKeyEvent(dpy, shiftCode, True, CurrentTime);
    XTestFakeKeyEvent(dpy, code, True, CurrentTime);
    XTestFakeKeyEvent(dpy, code, False, CurrentTime);
    if (shift) XTestFakeKeyEvent(dpy, shiftCode, False, CurrentTime);
    XFlush(dpy);
    pause(40);
}

// Deepest window under (x, y) of `w`, with coordinates relative to it.
Window deepestChild(Display* dpy, Window w, int& x, int& y) {
    Window current = w;
    for (;;) {
        int cx = 0, cy = 0;
        Window child = None;
        XTranslateCoordinates(dpy, current, current, x, y, &cx, &cy, &child);
        if (child == None) return current;
        int tx = 0, ty = 0;
        Window unused;
        XTranslateCoordinates(dpy, current, child, x, y, &tx, &ty, &unused);
        current = child;
        x = tx;
        y = ty;
    }
}

void syntheticClick(Display* dpy, Window w, int x, int y, unsigned button) {
    const Window target = deepestChild(dpy, w, x, y);
    int rx = 0, ry = 0;
    Window unused;
    XTranslateCoordinates(dpy, target, DefaultRootWindow(dpy), x, y, &rx, &ry, &unused);
    auto send = [&](int type, unsigned state, long mask) {
        XEvent e{};
        e.xbutton.type = type;
        e.xbutton.display = dpy;
        e.xbutton.window = target;
        e.xbutton.root = DefaultRootWindow(dpy);
        e.xbutton.subwindow = None;
        e.xbutton.time = CurrentTime;
        e.xbutton.x = x;
        e.xbutton.y = y;
        e.xbutton.x_root = rx;
        e.xbutton.y_root = ry;
        e.xbutton.state = state;
        e.xbutton.button = button;
        e.xbutton.same_screen = True;
        XSendEvent(dpy, target, True, mask, &e);
        XFlush(dpy);
        pause(50);
    };
    XEvent m{};
    m.xmotion.type = MotionNotify;
    m.xmotion.display = dpy;
    m.xmotion.window = target;
    m.xmotion.root = DefaultRootWindow(dpy);
    m.xmotion.x = x;
    m.xmotion.y = y;
    m.xmotion.x_root = rx;
    m.xmotion.y_root = ry;
    m.xmotion.same_screen = True;
    XSendEvent(dpy, target, True, PointerMotionMask, &m);
    send(ButtonPress, 0, ButtonPressMask);
    send(ButtonRelease, Button1Mask << (button - 1), ButtonReleaseMask);
}

int usage() {
    std::fputs("usage: opense4-observe list | click <win> <x> <y> [button] | dclick <win> <x> <y> | "
               "key <win> <keysym>... | type <win> <text> | move <win> <x> <y>\n",
               stderr);
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        std::fputs("cannot open X display (is DISPLAY set?)\n", stderr);
        return 1;
    }
    int ev, err, major, minor;
    if (!XTestQueryExtension(dpy, &ev, &err, &major, &minor)) {
        std::fputs("XTest extension not available\n", stderr);
        return 1;
    }
    const std::string cmd = argv[1];

    if (cmd == "list") {
        for (Window w : clientWindows(dpy)) {
            XWindowAttributes a{};
            XGetWindowAttributes(dpy, w, &a);
            int rx = 0, ry = 0;
            Window child;
            XTranslateCoordinates(dpy, w, DefaultRootWindow(dpy), 0, 0, &rx, &ry, &child);
            std::printf("0x%lx  %4dx%-4d at %5d,%-5d %s  %s\n", w, a.width, a.height, rx, ry,
                        a.map_state == IsViewable ? "visible" : "hidden ", windowName(dpy, w).c_str());
        }
        return 0;
    }
    if (argc < 3) return usage();
    const Window w = findWindow(dpy, argv[2]);
    if (w == None) {
        std::fprintf(stderr, "window not found: %s\n", argv[2]);
        return 1;
    }
    focus(dpy, w);

    if ((cmd == "click" || cmd == "dclick" || cmd == "move") && argc >= 5) {
        moveTo(dpy, w, std::atoi(argv[3]), std::atoi(argv[4]));
        if (cmd == "move") return 0;
        const unsigned button = argc >= 6 ? static_cast<unsigned>(std::atoi(argv[5])) : 1u;
        click(dpy, button);
        if (cmd == "dclick") click(dpy, button);
    } else if (cmd == "sclick" && argc >= 5) {
        syntheticClick(dpy, w, std::atoi(argv[3]), std::atoi(argv[4]), argc >= 6 ? static_cast<unsigned>(std::atoi(argv[5])) : 1u);
    } else if (cmd == "key" && argc >= 4) {
        for (int i = 3; i < argc; ++i) {
            const KeySym sym = XStringToKeysym(argv[i]);
            if (sym == NoSymbol) std::fprintf(stderr, "unknown keysym %s\n", argv[i]);
            else pressKey(dpy, sym, false);
        }
    } else if (cmd == "type" && argc >= 4) {
        for (const char* p = argv[3]; *p; ++p) {
            const char c = *p;
            char name[2] = {c, 0};
            KeySym sym = c == ' ' ? XK_space : c == '.' ? XK_period : c == '-' ? XK_minus : XStringToKeysym(name);
            const bool shift = (c >= 'A' && c <= 'Z');
            if (shift) sym = XStringToKeysym(std::string(1, static_cast<char>(c - 'A' + 'a')).c_str());
            pressKey(dpy, sym, shift);
        }
    } else {
        return usage();
    }
    XCloseDisplay(dpy);
    return 0;
}
