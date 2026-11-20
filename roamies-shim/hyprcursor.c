// Roamies on Hyprland: under XWayland an X11 app only sees the cursor while it
// is over an X11 window. Roamies polls gdk_device_get_position() to know when
// the cursor is over its companion, so feed it Hyprland's real cursor position.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

static int cached_x, cached_y; static double cached_at = -1;

static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

static int hypr_cursor(int *x, int *y) {
    double n = now_s();
    if (cached_at >= 0 && n - cached_at < 0.008) { *x = cached_x; *y = cached_y; return 1; }
    const char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE"), *rt = getenv("XDG_RUNTIME_DIR");
    if (!sig || !rt) return 0;
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    snprintf(a.sun_path, sizeof a.sun_path, "%s/hypr/%s/.socket.sock", rt, sig);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return 0;
    struct timeval tv = { 0, 50000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    char buf[64] = {0}; int ok = 0;
    if (connect(fd, (struct sockaddr *)&a, sizeof a) == 0 && write(fd, "cursorpos", 9) == 9) {
        ssize_t r = read(fd, buf, sizeof buf - 1);
        if (r > 0 && sscanf(buf, "%d, %d", x, y) == 2) ok = 1;
    }
    close(fd);
    if (ok) { cached_x = *x; cached_y = *y; cached_at = n; }
    return ok;
}

static void dbg(const char *k, int x, int y) {
    static int n; const char *p = getenv("ROAMIES_SHIM_DEBUG");
    if (!p || n++ > 400) return;
    FILE *f = fopen(p, "a"); if (f) { fprintf(f, "%s %d %d\n", k, x, y); fclose(f); }
}

void gdk_device_get_position(void *dev, void **screen, int *x, int *y) {
    static void (*real)(void *, void **, int *, int *);
    if (!real) real = dlsym(RTLD_NEXT, "gdk_device_get_position");
    real(dev, screen, x, y);
    int hx, hy;
    if (hypr_cursor(&hx, &hy)) { if (x) *x = hx; if (y) *y = hy; }
    dbg("int", x ? *x : -1, y ? *y : -1);
}

void gdk_device_get_position_double(void *dev, void **screen, double *x, double *y) {
    static void (*real)(void *, void **, double *, double *);
    if (!real) real = dlsym(RTLD_NEXT, "gdk_device_get_position_double");
    real(dev, screen, x, y);
    int hx, hy;
    if (hypr_cursor(&hx, &hy)) { if (x) *x = hx; if (y) *y = hy; }
    dbg("dbl", x ? (int)*x : -1, y ? (int)*y : -1);
}
