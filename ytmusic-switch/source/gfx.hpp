#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <list>
#include <map>
#include <tuple>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Noctalia palette (same defaults as the desktop app)
namespace Col {
constexpr SDL_Color primary{0xff, 0xf5, 0x9b, 255}, onPrimary{0x0e, 0x0e, 0x43, 255}, secondary{0xa9, 0xae, 0xfe, 255},
    tertiary{0x9b, 0xfe, 0xce, 255}, surface{0x07, 0x07, 0x22, 255}, surfaceVariant{0x11, 0x11, 0x2d, 255},
    onSurface{0xf3, 0xed, 0xf7, 255}, onSurfaceVariant{0x7c, 0x80, 0xb4, 255}, outline{0x21, 0x21, 0x5f, 255}, error{0xfd, 0x46, 0x63, 255};
inline SDL_Color alpha(SDL_Color c, Uint8 a) { c.a = a; return c; }
}

enum class Align { Left, Center, Right };

class Gfx {
public:
    struct FontBlob { const void *data; size_t size; };
    // first font is the main one; the rest are fallbacks (CJK, Korean, ...) used for text the main one can't show
    bool init(SDL_Renderer *r, const std::vector<FontBlob> &fonts);
    void shutdown();
    SDL_Renderer *r = nullptr;

    void fillRect(float x, float y, float w, float h, SDL_Color c);
    void fillRound(float x, float y, float w, float h, float rad, SDL_Color c);
    void strokeRound(float x, float y, float w, float h, float rad, float t, SDL_Color c);
    void fillCircle(float cx, float cy, float rad, SDL_Color c);
    // text: returns drawn width. maxW > 0 elides with "…"
    int text(const std::string &s, float x, float y, int size, SDL_Color c, int maxW = 0, Align a = Align::Left, bool bold = false);
    int measure(const std::string &s, int size, bool bold = false);
    void image(SDL_Texture *t, float x, float y, float w, float h, float rad = 0);   // cover-fit, rounded corners via mask
    void endFrame();   // evicts unused cached text

    // glyphs drawn with primitives (no icon font needed)
    void icon(const std::string &name, float cx, float cy, float s, SDL_Color c);
    // controller button badge, e.g. "A", "ZR", "+"
    int badge(const std::string &label, float x, float y, SDL_Color bg = Col::outline, SDL_Color fg = Col::onSurface);

private:
    TTF_Font *font(int size, bool bold, int which = 0);
    std::string pickFont(const std::string &s, int size, bool bold, int &which);
    struct Cached { SDL_Texture *tex; int w, h; unsigned frame; };
    std::unordered_map<std::string, Cached> cache;
    std::map<std::tuple<int, bool, int>, TTF_Font *> fonts;
    std::vector<FontBlob> blobs;
    unsigned frameNo = 0;
    SDL_Texture *maskTex = nullptr;
};

// Album art from i.ytimg.com, fetched by two worker threads, turned into textures on the main thread.
class Thumbs {
public:
    void start(SDL_Renderer *r);
    void stop();
    SDL_Texture *get(const std::string &vid);   // nullptr until loaded
    void pump();                                 // main thread: upload finished downloads
private:
    void worker();
    SDL_Renderer *r = nullptr;
    std::mutex mu;
    std::condition_variable cv;
    std::deque<std::string> want;
    std::vector<std::pair<std::string, std::string>> ready;   // vid, jpeg bytes
    std::unordered_map<std::string, SDL_Texture *> tex;
    std::list<std::string> lru;
    std::unordered_map<std::string, bool> requested;
    std::vector<std::thread> th;
    std::atomic<bool> quit{false};
};
