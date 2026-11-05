// Renders the Switch UI on Linux (software renderer, no window) and saves screenshots: CI checks the layout with it.
#include "../source/app.hpp"
#include "../source/net.hpp"
#include <SDL2/SDL_image.h>
#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>

static void shot(SDL_Renderer *r, SDL_Surface *s, const char *name) {
    SDL_RenderPresent(r);
    IMG_SavePNG(s, name);
    printf("saved %s\n", name);
}

int main(int argc, char **argv) {
    const char *fontPath = argc > 1 ? argv[1] : "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    std::ifstream f(fontPath, std::ios::binary);
    std::string font((std::istreambuf_iterator<char>(f)), {});
    SDL_Init(SDL_INIT_AUDIO);
    TTF_Init();
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
    Net::init();
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = SDL_CreateSoftwareRenderer(surf);
    App app;
    app.keyboard = [](const std::string &, const std::string &, std::string &out) { out = "test"; return true; };
    app.battery = [] { return 82; };
    if (!app.init(r, {{font.data(), font.size()}})) { printf("init failed\n"); return 1; }
    auto run = [&](float secs, Input in = {}) {
        const int frames = int(secs * 30);
        for (int i = 0; i < frames; ++i) {
            Input x = in;
            if (i > 0) x.down = x.held = 0;
            x.dt = 1 / 30.f;
            app.frame(x);
            app.render();
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
    };
    run(0.3f);
    shot(r, surf, "ui-empty.png");
    app.testSearch("daft punk");
    run(4);
    Input a; a.down = B_A;                 // play first result
    run(0.2f, a);
    run(5);
    shot(r, surf, "ui-search-playing.png");
    Input right; right.down = right.held = B_RIGHT;
    run(0.1f, right); run(0.1f, right);
    run(0.5f);
    shot(r, surf, "ui-tiles.png");
    Input minus; minus.down = B_MINUS;     // context menu
    run(0.3f, minus);
    shot(r, surf, "ui-menu.png");
    Input b; b.down = B_B;
    run(0.2f, b);
    app.testView(1);                       // queue
    run(1);
    shot(r, surf, "ui-queue.png");
    app.testPlay({"dQw4w9WgXcQ", "Rick Astley - Never Gonna Give You Up", "Rick Astley", 213});
    run(5);
    printf("app play path: state=%d pos=%.1f (2 = Playing)\n", app.testState(), app.testPos());
    shot(r, surf, "ui-playing.png");
    app.testView(2);                       // focus on the section buttons
    app.testFocusTabs();
    run(0.5f);
    shot(r, surf, "ui-tabs.png");
    app.testView(5);                       // settings
    Input a2; a2.down = B_A;
    run(0.3f);
    shot(r, surf, "ui-settings.png");
    setDarkTheme(false);
    app.testView(1);
    run(0.5f);
    shot(r, surf, "ui-light.png");
    setDarkTheme(true);
    // docked: render the home row at 1920x1080
    {
        SDL_Surface *big = SDL_CreateRGBSurfaceWithFormat(0, 1920, 1080, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_Renderer *br = SDL_CreateSoftwareRenderer(big);
        App app2;
        app2.keyboard = app.keyboard;
        app2.battery = [] { return 82; };
        if (app2.init(br, {{font.data(), font.size()}})) {
            app2.setScale(1.5f);
            app2.testSearch("daft punk");
            for (int i = 0; i < 150; ++i) { Input x; x.dt = 1 / 30.f; app2.frame(x); app2.render(); std::this_thread::sleep_for(std::chrono::milliseconds(33)); }
            Input rt; rt.down = rt.held = B_RIGHT; rt.dt = 1 / 30.f;
            app2.frame(rt);
            for (int i = 0; i < 20; ++i) { Input x; x.dt = 1 / 30.f; app2.frame(x); app2.render(); }
            shot(br, big, "ui-1080p.png");
            app2.shutdown();
        }
    }
    app.shutdown();
    return 0;
}
