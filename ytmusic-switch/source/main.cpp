// YT Music for Nintendo Switch (homebrew, libnx + SDL2)
#include "app.hpp"
#include "net.hpp"
#include <SDL2/SDL_image.h>
#include <switch.h>

static bool showKeyboard(const std::string &guide, const std::string &initial, std::string &out) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) return false;
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, guide.c_str());
    swkbdConfigSetInitialText(&kbd, initial.c_str());
    swkbdConfigSetStringLenMax(&kbd, 200);
    char buf[512] = {0};
    const Result rc = swkbdShow(&kbd, buf, sizeof buf);
    swkbdClose(&kbd);
    out = buf;
    return R_SUCCEEDED(rc) && !out.empty();
}

int main(int, char **) {
    socketInitializeDefault();
    psmInitialize();
    plInitialize(PlServiceType_User);
    // system fonts: Latin/Japanese first, then Chinese, Korean and Nintendo's symbols as fallbacks
    std::vector<Gfx::FontBlob> fonts;
    for (PlSharedFontType t : {PlSharedFontType_Standard, PlSharedFontType_ChineseSimplified, PlSharedFontType_ExtChineseSimplified,
                               PlSharedFontType_ChineseTraditional, PlSharedFontType_KO, PlSharedFontType_NintendoExt}) {
        PlFontData fd{};
        if (R_SUCCEEDED(plGetSharedFontByType(&fd, t)) && fd.address) fonts.push_back({fd.address, fd.size});
    }

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    TTF_Init();
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
    // Render at the console's real resolution: 1920x1080 docked, 1280x720 handheld (no upscaling blur)
    auto docked = [] { return appletGetOperationMode() == AppletOperationMode_Console; };
    bool wasDocked = docked();
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");   // smooth (linear) texture scaling
    SDL_Window *win = SDL_CreateWindow("YT Music", 0, 0, wasDocked ? 1920 : 1280, wasDocked ? 1080 : 720, 0);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();
    Net::init();
    appletSetMediaPlaybackState(true);   // don't dim / sleep the screen while we play music

    App app;
    app.keyboard = showKeyboard;
    app.battery = [] {
        u32 p = 0;
        return R_SUCCEEDED(psmGetBatteryChargePercentage(&p)) ? int(p) : -1;
    };
    if (!app.init(ren, fonts)) {
        logf("init failed");
    } else {
        Uint64 last = SDL_GetPerformanceCounter();
        while (appletMainLoop()) {
            SDL_Event e;
            bool quit = false;
            while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT) quit = true;
            if (quit) break;
            if (docked() != wasDocked) {   // docked / undocked while running
                wasDocked = docked();
                SDL_SetWindowSize(win, wasDocked ? 1920 : 1280, wasDocked ? 1080 : 720);
            }
            int ow = 1280, oh = 720;
            SDL_GetRendererOutputSize(ren, &ow, &oh);
            app.setScale(ow / 1280.f);
            padUpdate(&pad);
            const u64 down = padGetButtonsDown(&pad), held = padGetButtons(&pad);
            auto map = [](u64 b) {
                uint32_t o = 0;
                if (b & HidNpadButton_A) o |= B_A;
                if (b & HidNpadButton_B) o |= B_B;
                if (b & HidNpadButton_X) o |= B_X;
                if (b & HidNpadButton_Y) o |= B_Y;
                if (b & HidNpadButton_L) o |= B_L;
                if (b & HidNpadButton_R) o |= B_R;
                if (b & HidNpadButton_ZL) o |= B_ZL;
                if (b & HidNpadButton_ZR) o |= B_ZR;
                if (b & HidNpadButton_Plus) o |= B_PLUS;
                if (b & HidNpadButton_Minus) o |= B_MINUS;
                if (b & (HidNpadButton_Up)) o |= B_UP;
                if (b & (HidNpadButton_Down)) o |= B_DOWN;
                if (b & (HidNpadButton_Left)) o |= B_LEFT;
                if (b & (HidNpadButton_Right)) o |= B_RIGHT;
                return o;
            };
            Input in;
            in.down = map(down);
            in.held = map(held);
            const HidAnalogStickState ls = padGetStickPos(&pad, 0), rs = padGetStickPos(&pad, 1);
            in.lx = ls.x / 32767.f; in.ly = ls.y / 32767.f;
            in.rx = rs.x / 32767.f; in.ry = rs.y / 32767.f;
            HidTouchScreenState ts{};
            if (hidGetTouchScreenStates(&ts, 1) && ts.count > 0) {
                in.touching = true;
                in.tx = float(ts.touches[0].x);
                in.ty = float(ts.touches[0].y);
            }
            const Uint64 now = SDL_GetPerformanceCounter();
            in.dt = std::min(0.1f, float(now - last) / float(SDL_GetPerformanceFrequency()));
            last = now;
            if (!app.frame(in)) break;
            app.render();
            SDL_RenderPresent(ren);
        }
    }
    app.shutdown();
    appletSetMediaPlaybackState(false);
    Net::cleanup();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    plExit();
    psmExit();
    socketExit();
    return 0;
}
