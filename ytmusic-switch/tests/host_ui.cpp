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
    if (!app.init(r, {{font.data(), font.size()}})) { printf("init failed\n"); return 1; }
    auto run = [&](float secs, Input in = {}) {
        const int frames = int(secs * 30);
        for (int i = 0; i < frames; ++i) {
            Input x = in;
            if (i > 0) x.down = 0;
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
    Input down; down.down = B_DOWN;
    run(0.1f, down); run(0.1f, down);
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
    app.testView(5);                       // settings
    run(0.3f);
    shot(r, surf, "ui-settings.png");
    app.shutdown();
    return 0;
}
