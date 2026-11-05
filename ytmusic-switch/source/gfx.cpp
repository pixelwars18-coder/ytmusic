#include "gfx.hpp"
#include "net.hpp"
#include "yt.hpp"
#include <SDL2/SDL_image.h>
#include <algorithm>
#include <cmath>

static SDL_Vertex vtx(float x, float y, SDL_Color c) { return SDL_Vertex{{x, y}, c, {0, 0}}; }

static const Theme kDark{{0x26, 0x27, 0x2b, 255}, {0x34, 0x35, 0x3a, 255}, {0x43, 0x44, 0x4a, 255}, {0xff, 0xff, 0xff, 255},
                         {0xa8, 0xaa, 0xb0, 255}, {0x4a, 0x4b, 0x52, 255}, {0x2d, 0xc8, 0xf0, 255}, {0x0b, 0x1e, 0x26, 255},
                         {0x00, 0x00, 0x00, 170}, {0xff, 0x5a, 0x5f, 255}};
static const Theme kLight{{0xeb, 0xeb, 0xed, 255}, {0xff, 0xff, 0xff, 255}, {0xf3, 0xf3, 0xf5, 255}, {0x2d, 0x2d, 0x30, 255},
                          {0x70, 0x71, 0x78, 255}, {0xcf, 0xd0, 0xd5, 255}, {0x00, 0x8f, 0xd6, 255}, {0xff, 0xff, 0xff, 255},
                          {0x20, 0x20, 0x28, 120}, {0xe0, 0x30, 0x3a, 255}};
static bool gDark = true;
const Theme &TH() { return gDark ? kDark : kLight; }
void setDarkTheme(bool d) { gDark = d; }
bool darkTheme() { return gDark; }

static SDL_Color hsv(float h, float s, float v, Uint8 a = 255) {
    h = std::fmod(h, 1.f);
    if (h < 0) h += 1;
    const float i = std::floor(h * 6), f = h * 6 - i, p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    float r, g, b;
    switch (int(i) % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return SDL_Color{Uint8(r * 255), Uint8(g * 255), Uint8(b * 255), a};
}

bool Gfx::init(SDL_Renderer *rr, const std::vector<FontBlob> &f) {
    r = rr;
    blobs = f;
    if (blobs.empty()) return false;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    return font(16, false, 0) != nullptr;
}

void Gfx::shutdown() {
    for (auto &[k, c] : cache) SDL_DestroyTexture(c.tex);
    cache.clear();
    for (auto &[k, f] : fonts) TTF_CloseFont(f);
    fonts.clear();
}

TTF_Font *Gfx::font(int size, bool bold, int which) {
    if (which < 0 || which >= int(blobs.size())) which = 0;
    auto key = std::make_tuple(size, bold, which);
    auto it = fonts.find(key);
    if (it != fonts.end()) return it->second;
    TTF_Font *f = blobs[size_t(which)].data ? TTF_OpenFontRW(SDL_RWFromConstMem(blobs[size_t(which)].data, int(blobs[size_t(which)].size)), 1, size) : nullptr;
    if (f && bold) TTF_SetFontStyle(f, TTF_STYLE_BOLD);
    if (f) TTF_SetFontHinting(f, TTF_HINTING_LIGHT);
    fonts[key] = f;
    return f;
}

void Gfx::fillRect(float x, float y, float w, float h, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_FRect fr{x, y, w, h};
    SDL_RenderFillRectF(r, &fr);
}

// Rounded rectangle as one triangle fan (corners: 8 segments each)
void Gfx::fillRound(float x, float y, float w, float h, float rad, SDL_Color c) {
    rad = std::min({rad, w / 2, h / 2});
    if (rad < 1) { fillRect(x, y, w, h, c); return; }
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    v.push_back(vtx(x + w / 2, y + h / 2, c));
    const float cx[4] = {x + w - rad, x + rad, x + rad, x + w - rad}, cy[4] = {y + rad, y + rad, y + h - rad, y + h - rad};
    const int seg = rad > 12 ? 10 : 6;
    for (int q = 0; q < 4; ++q)
        for (int i = 0; i <= seg; ++i) {
            const float a = float(M_PI) / 2 * (q + float(i) / seg);
            v.push_back(vtx(cx[q] + std::cos(a) * rad, cy[q] - std::sin(a) * rad, c));
        }
    const int n = int(v.size()) - 1;
    for (int i = 1; i <= n; ++i) { idx.push_back(0); idx.push_back(i); idx.push_back(i % n + 1); }
    SDL_RenderGeometry(r, nullptr, v.data(), int(v.size()), idx.data(), int(idx.size()));
}

void Gfx::strokeRound(float x, float y, float w, float h, float rad, float t, SDL_Color c) {
    // ring between two rounded outlines
    rad = std::min({rad, w / 2, h / 2});
    const float ri = std::max(0.f, rad - t);
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    const float cx[4] = {x + w - rad, x + rad, x + rad, x + w - rad}, cy[4] = {y + rad, y + rad, y + h - rad, y + h - rad};
    const int seg = 8;
    for (int q = 0; q < 4; ++q)
        for (int i = 0; i <= seg; ++i) {
            const float a = float(M_PI) / 2 * (q + float(i) / seg);
            const float ca = std::cos(a), sa = std::sin(a);
            v.push_back(vtx(cx[q] + ca * rad, cy[q] - sa * rad, c));
            v.push_back(vtx(cx[q] + ca * ri, cy[q] - sa * ri, c));
        }
    const int n = int(v.size()) / 2;
    for (int i = 0; i < n; ++i) {
        const int a = 2 * i, b = 2 * ((i + 1) % n);
        idx.insert(idx.end(), {a, a + 1, b, b, a + 1, b + 1});
    }
    SDL_RenderGeometry(r, nullptr, v.data(), int(v.size()), idx.data(), int(idx.size()));
}

void Gfx::fillCircle(float cx, float cy, float rad, SDL_Color c) { fillRound(cx - rad, cy - rad, rad * 2, rad * 2, rad, c); }

// Ring around a rounded rect whose colour runs through the spectrum along the outline and slowly rotates.
void Gfx::rainbowRing(float x, float y, float w, float h, float rad, float t, float phase) {
    rad = std::min({rad, w / 2, h / 2});
    const float pulse = 0.5f + 0.5f * std::sin(phase * 3.2f);
    const float outR = rad + t, inR = rad;
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    const float cx[4] = {x + w - rad, x + rad, x + rad, x + w - rad}, cy[4] = {y + rad, y + rad, y + h - rad, y + h - rad};
    const int seg = 10;
    const int n = 4 * (seg + 1);
    for (int q = 0; q < 4; ++q)
        for (int i = 0; i <= seg; ++i) {
            const int k = q * (seg + 1) + i;
            const float a = float(M_PI) / 2 * (q + float(i) / seg);
            const float ca = std::cos(a), sa = std::sin(a);
            const SDL_Color c = hsv(float(k) / n + phase * 0.12f, 0.62f, 1.0f, Uint8(200 + 55 * pulse));
            v.push_back(vtx(cx[q] + ca * outR, cy[q] - sa * outR, c));
            v.push_back(vtx(cx[q] + ca * inR, cy[q] - sa * inR, c));
        }
    for (int i = 0; i < n; ++i) {
        const int a = 2 * i, b = 2 * ((i + 1) % n);
        idx.insert(idx.end(), {a, a + 1, b, b, a + 1, b + 1});
    }
    SDL_RenderGeometry(r, nullptr, v.data(), int(v.size()), idx.data(), int(idx.size()));
}

// Soft drop shadow: a few expanding translucent rounded rects.
void Gfx::shadow(float x, float y, float w, float h, float rad, float spread, Uint8 alpha) {
    for (int i = 4; i >= 1; --i) {
        const float e = spread * i / 4;
        fillRound(x - e, y - e + spread * 0.4f, w + 2 * e, h + 2 * e, rad + e, SDL_Color{0, 0, 0, Uint8(alpha / 4)});
    }
}

// UTF-8 aware trim to fit
static std::string elide(TTF_Font *f, const std::string &s, int maxW) {
    int w = 0;
    TTF_SizeUTF8(f, s.c_str(), &w, nullptr);
    if (w <= maxW) return s;
    std::string cut = s;
    while (!cut.empty()) {
        size_t i = cut.size() - 1;   // drop one whole UTF-8 code point
        while (i > 0 && (static_cast<unsigned char>(cut[i]) & 0xC0) == 0x80) --i;
        cut.resize(i);
        while (!cut.empty() && cut.back() == ' ') cut.pop_back();
        std::string t = cut + "\xE2\x80\xA6";
        TTF_SizeUTF8(f, t.c_str(), &w, nullptr);
        if (w <= maxW) return t;
    }
    return "\xE2\x80\xA6";
}

static std::vector<uint32_t> decodeUtf8(const std::string &s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp;
        int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1f; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0f; n = 3; }
        else if ((c >> 3) == 30) { cp = c & 0x07; n = 4; }
        else { ++i; continue; }
        if (i + size_t(n) > s.size()) break;
        for (int k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + size_t(k)]) & 0x3f);
        out.push_back(cp);
        i += size_t(n);
    }
    return out;
}

static void encodeUtf8(uint32_t cp, std::string &o) {
    if (cp < 0x80) o += char(cp);
    else if (cp < 0x800) { o += char(0xC0 | (cp >> 6)); o += char(0x80 | (cp & 0x3f)); }
    else if (cp < 0x10000) { o += char(0xE0 | (cp >> 12)); o += char(0x80 | ((cp >> 6) & 0x3f)); o += char(0x80 | (cp & 0x3f)); }
    else { o += char(0xF0 | (cp >> 18)); o += char(0x80 | ((cp >> 12) & 0x3f)); o += char(0x80 | ((cp >> 6) & 0x3f)); o += char(0x80 | (cp & 0x3f)); }
}

// Picks the font that can show the most of `s` (the Switch has separate Japanese/Chinese/Korean system fonts)
// and drops characters none of them have (emoji), instead of drawing boxes.
std::string Gfx::pickFont(const std::string &s, int size, bool bold, int &which) {
    which = 0;
    bool ascii = true;
    for (unsigned char c : s) if (c >= 0x80) { ascii = false; break; }
    if (ascii) return s;
    const auto cps = decodeUtf8(s);
    int best = -1, bestIdx = 0;
    for (int i = 0; i < int(blobs.size()); ++i) {
        TTF_Font *f = font(size, bold, i);
        if (!f) continue;
        int have = 0;
        for (uint32_t cp : cps) have += (cp < 0x80 || TTF_GlyphIsProvided32(f, cp)) ? 1 : 0;
        if (have > best) { best = have; bestIdx = i; }
        if (have == int(cps.size())) break;
    }
    which = bestIdx;
    if (best == int(cps.size())) return s;
    TTF_Font *f = font(size, bold, which);
    std::string out;
    for (uint32_t cp : cps) if (cp < 0x80 || TTF_GlyphIsProvided32(f, cp)) encodeUtf8(cp, out);
    std::string trimmed;   // collapse doubled spaces left behind by removed emoji
    for (char ch : out) if (!(ch == ' ' && !trimmed.empty() && trimmed.back() == ' ')) trimmed += ch;
    while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
    return trimmed;
}

int Gfx::measure(const std::string &s0, int size, bool bold) {
    int w = 0, which = 0;
    const std::string s = pickFont(s0, size, bold, which);
    if (TTF_Font *f = font(size, bold, which)) TTF_SizeUTF8(f, s.c_str(), &w, nullptr);
    return w;
}

int Gfx::text(const std::string &s0, float x, float y, int size, SDL_Color c, int maxW, Align a, bool bold) {
    if (s0.empty()) return 0;
    int which = 0;
    const std::string s1 = pickFont(s0, size, bold, which);
    TTF_Font *f = font(size, bold, which);
    if (!f || s1.empty()) return 0;
    std::string s = maxW > 0 ? elide(f, s1, maxW) : s1;
    char key[64];
    snprintf(key, sizeof key, "|%d|%d|%d|%02x%02x%02x", size, int(bold), which, c.r, c.g, c.b);
    const std::string k = s + key;
    auto it = cache.find(k);
    if (it == cache.end()) {
        SDL_Surface *surf = TTF_RenderUTF8_Blended(f, s.c_str(), SDL_Color{c.r, c.g, c.b, 255});
        if (!surf) return 0;
        SDL_Texture *t = SDL_CreateTextureFromSurface(r, surf);
        Cached cc{t, surf->w, surf->h, frameNo};
        SDL_FreeSurface(surf);
        it = cache.emplace(k, cc).first;
    }
    it->second.frame = frameNo;
    SDL_SetTextureAlphaMod(it->second.tex, c.a);
    float dx = x;
    if (a == Align::Center) dx = x - it->second.w / 2.f;
    else if (a == Align::Right) dx = x - it->second.w;
    SDL_FRect dst{std::round(dx), std::round(y), float(it->second.w), float(it->second.h)};
    SDL_RenderCopyF(r, it->second.tex, nullptr, &dst);
    return it->second.w;
}

void Gfx::image(SDL_Texture *t, float x, float y, float w, float h, float rad) {
    if (!t) return;
    int tw, th;
    SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
    // cover-fit crop
    const float sa = float(tw) / th, da = w / h;
    SDL_Rect src{0, 0, tw, th};
    if (sa > da) { src.w = int(th * da); src.x = (tw - src.w) / 2; }
    else { src.h = int(tw / da); src.y = (th - src.h) / 2; }
    if (rad <= 0) {
        SDL_FRect dst{x, y, w, h};
        SDL_RenderCopyF(r, t, &src, &dst);
        return;
    }
    // textured rounded fan
    rad = std::min({rad, w / 2, h / 2});
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    auto uv = [&](float px, float py) {
        return SDL_FPoint{(src.x + (px - x) / w * src.w) / tw, (src.y + (py - y) / h * src.h) / th};
    };
    SDL_Color white{255, 255, 255, 255};
    v.push_back(SDL_Vertex{{x + w / 2, y + h / 2}, white, uv(x + w / 2, y + h / 2)});
    const float cx[4] = {x + w - rad, x + rad, x + rad, x + w - rad}, cy[4] = {y + rad, y + rad, y + h - rad, y + h - rad};
    for (int q = 0; q < 4; ++q)
        for (int i = 0; i <= 6; ++i) {
            const float a = float(M_PI) / 2 * (q + i / 6.f);
            const float px = cx[q] + std::cos(a) * rad, py = cy[q] - std::sin(a) * rad;
            v.push_back(SDL_Vertex{{px, py}, white, uv(px, py)});
        }
    const int n = int(v.size()) - 1;
    for (int i = 1; i <= n; ++i) { idx.push_back(0); idx.push_back(i); idx.push_back(i % n + 1); }
    SDL_RenderGeometry(r, t, v.data(), int(v.size()), idx.data(), int(idx.size()));
}

void Gfx::endFrame() {
    ++frameNo;
    if (cache.size() < 400) return;
    for (auto it = cache.begin(); it != cache.end();) {
        if (frameNo - it->second.frame > 120) { SDL_DestroyTexture(it->second.tex); it = cache.erase(it); }
        else ++it;
    }
}

static void thickLine(SDL_Renderer *r, float x1, float y1, float x2, float y2, float t, SDL_Color c) {
    const float dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0) return;
    const float nx = -dy / len * t / 2, ny = dx / len * t / 2;
    SDL_Vertex v[4] = {vtx(x1 + nx, y1 + ny, c), vtx(x1 - nx, y1 - ny, c), vtx(x2 + nx, y2 + ny, c), vtx(x2 - nx, y2 - ny, c)};
    int idx[6] = {0, 1, 2, 2, 1, 3};
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
}

static void tri(SDL_Renderer *r, float x1, float y1, float x2, float y2, float x3, float y3, SDL_Color c) {
    SDL_Vertex v[3] = {vtx(x1, y1, c), vtx(x2, y2, c), vtx(x3, y3, c)};
    SDL_RenderGeometry(r, nullptr, v, 3, nullptr, 0);
}

void Gfx::icon(const std::string &n, float cx, float cy, float s, SDL_Color c) {
    const float h = s / 2, t = std::max(2.f, s / 10);
    if (n == "play") tri(r, cx - h * 0.6f, cy - h * 0.8f, cx - h * 0.6f, cy + h * 0.8f, cx + h * 0.8f, cy, c);
    else if (n == "pause") { fillRound(cx - h * 0.7f, cy - h * 0.75f, h * 0.5f, h * 1.5f, 2, c); fillRound(cx + h * 0.2f, cy - h * 0.75f, h * 0.5f, h * 1.5f, 2, c); }
    else if (n == "next") { tri(r, cx - h * 0.7f, cy - h * 0.7f, cx - h * 0.7f, cy + h * 0.7f, cx + h * 0.4f, cy, c); fillRect(cx + h * 0.45f, cy - h * 0.7f, t, h * 1.4f, c); }
    else if (n == "prev") { tri(r, cx + h * 0.7f, cy - h * 0.7f, cx + h * 0.7f, cy + h * 0.7f, cx - h * 0.4f, cy, c); fillRect(cx - h * 0.45f - t, cy - h * 0.7f, t, h * 1.4f, c); }
    else if (n == "heart") {
        fillCircle(cx - h * 0.4f, cy - h * 0.25f, h * 0.45f, c);
        fillCircle(cx + h * 0.4f, cy - h * 0.25f, h * 0.45f, c);
        tri(r, cx - h * 0.84f, cy - h * 0.1f, cx + h * 0.84f, cy - h * 0.1f, cx, cy + h * 0.85f, c);
    } else if (n == "search") {
        strokeRound(cx - h * 0.8f, cy - h * 0.8f, h * 1.25f, h * 1.25f, h * 0.63f, t, c);
        thickLine(r, cx + h * 0.25f, cy + h * 0.25f, cx + h * 0.8f, cy + h * 0.8f, t * 1.2f, c);
    } else if (n == "list") {
        for (int i = -1; i <= 1; ++i) { fillCircle(cx - h * 0.7f, cy + i * h * 0.55f, t * 0.7f, c); fillRect(cx - h * 0.35f, cy + i * h * 0.55f - t / 2, h * 1.1f, t, c); }
    } else if (n == "queue") {
        for (int i = -1; i <= 1; ++i) fillRect(cx - h * 0.8f, cy + i * h * 0.55f - t / 2, i == 1 ? h * 0.8f : h * 1.3f, t, c);
        tri(r, cx + h * 0.3f, cy + h * 0.25f, cx + h * 0.3f, cy + h * 0.85f, cx + h * 0.85f, cy + h * 0.55f, c);
    } else if (n == "download") {
        fillRect(cx - t / 2, cy - h * 0.8f, t, h * 1.1f, c);
        tri(r, cx - h * 0.5f, cy + h * 0.05f, cx + h * 0.5f, cy + h * 0.05f, cx, cy + h * 0.5f, c);
        fillRect(cx - h * 0.8f, cy + h * 0.7f, h * 1.6f, t, c);
    } else if (n == "check") {
        thickLine(r, cx - h * 0.6f, cy, cx - h * 0.15f, cy + h * 0.45f, t * 1.3f, c);
        thickLine(r, cx - h * 0.15f, cy + h * 0.45f, cx + h * 0.65f, cy - h * 0.45f, t * 1.3f, c);
    } else if (n == "gear") {
        strokeRound(cx - h * 0.55f, cy - h * 0.55f, h * 1.1f, h * 1.1f, h * 0.55f, t * 1.2f, c);
        for (int i = 0; i < 8; ++i) {
            const float a = float(M_PI) / 4 * i;
            thickLine(r, cx + std::cos(a) * h * 0.55f, cy + std::sin(a) * h * 0.55f, cx + std::cos(a) * h * 0.9f, cy + std::sin(a) * h * 0.9f, t * 1.3f, c);
        }
    } else if (n == "music") {
        fillCircle(cx - h * 0.45f, cy + h * 0.5f, h * 0.32f, c);
        fillRect(cx - h * 0.2f, cy - h * 0.75f, t, h * 1.25f, c);
        thickLine(r, cx - h * 0.2f, cy - h * 0.72f, cx + h * 0.7f, cy - h * 0.45f, t * 1.4f, c);
    } else if (n == "shuffle") {
        thickLine(r, cx - h * 0.8f, cy - h * 0.5f, cx + h * 0.6f, cy + h * 0.5f, t, c);
        thickLine(r, cx - h * 0.8f, cy + h * 0.5f, cx + h * 0.6f, cy - h * 0.5f, t, c);
        tri(r, cx + h * 0.4f, cy - h * 0.85f, cx + h * 0.4f, cy - h * 0.15f, cx + h * 0.9f, cy - h * 0.5f, c);
        tri(r, cx + h * 0.4f, cy + h * 0.15f, cx + h * 0.4f, cy + h * 0.85f, cx + h * 0.9f, cy + h * 0.5f, c);
    } else if (n == "repeat") {
        strokeRound(cx - h * 0.8f, cy - h * 0.55f, h * 1.6f, h * 1.1f, h * 0.4f, t, c);
        tri(r, cx + h * 0.05f, cy - h * 0.85f, cx + h * 0.05f, cy - h * 0.25f, cx + h * 0.45f, cy - h * 0.55f, c);
    }
}

// Controller button glyph: filled circle (or pill for ZL/ZR/L/R) with the letter cut out in the background colour
int Gfx::badge(const std::string &label, float x, float y, SDL_Color bg, SDL_Color fg) {
    const int w = std::max(26, measure(label, 14, true) + 14);
    fillRound(x, y, float(w), 26, 13, bg);
    text(label, x + w / 2.f, y + 3, 14, fg, 0, Align::Center, true);
    return w;
}

// ------------------------------------------------------------------ Thumbs
void Thumbs::start(SDL_Renderer *rr) {
    r = rr;
    for (int i = 0; i < 2; ++i) th.emplace_back([this] { worker(); });
}

void Thumbs::stop() {
    quit = true;
    cv.notify_all();
    for (auto &t : th) if (t.joinable()) t.join();
    for (auto &[k, t] : tex) SDL_DestroyTexture(t);
    tex.clear();
}

SDL_Texture *Thumbs::get(const std::string &vid) {
    if (vid.empty()) return nullptr;
    auto it = tex.find(vid);
    if (it != tex.end()) {
        lru.remove(vid);
        lru.push_front(vid);
        return it->second;
    }
    std::lock_guard<std::mutex> l(mu);
    if (!requested[vid]) {
        requested[vid] = true;
        want.push_front(vid);   // newest first: what's on screen now
        if (want.size() > 40) { requested.erase(want.back()); want.pop_back(); }
        cv.notify_one();
    }
    return nullptr;
}

void Thumbs::pump() {
    std::vector<std::pair<std::string, std::string>> got;
    { std::lock_guard<std::mutex> l(mu); got.swap(ready); }
    for (auto &[vid, bytes] : got) {
        SDL_Surface *src = bytes.empty() ? nullptr : IMG_Load_RW(SDL_RWFromConstMem(bytes.data(), int(bytes.size())), 1);
        if (!src) continue;
        // hqdefault is 480x360 with the 16:9 picture letterboxed in the middle: keep a centred square of the picture
        const int ph = src->w * 9 / 16, side = std::min(ph, src->h);
        SDL_Rect cr{(src->w - side) / 2, (src->h - side) / 2, side, side};
        SDL_Surface *sq = SDL_CreateRGBSurfaceWithFormat(0, side, side, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_Texture *t = nullptr;
        if (sq) {
            SDL_BlitSurface(src, &cr, sq, nullptr);
            t = SDL_CreateTextureFromSurface(r, sq);
            SDL_FreeSurface(sq);
        }
        SDL_FreeSurface(src);
        if (!t) continue;
        SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
        tex[vid] = t;
        lru.push_front(vid);
        while (lru.size() > 120) {
            const std::string old = lru.back();
            lru.pop_back();
            SDL_DestroyTexture(tex[old]);
            tex.erase(old);
            std::lock_guard<std::mutex> l(mu);
            requested.erase(old);
        }
    }
}

void Thumbs::worker() {
    while (!quit) {
        std::string vid;
        {
            std::unique_lock<std::mutex> l(mu);
            cv.wait(l, [&] { return quit || !want.empty(); });
            if (quit) return;
            vid = want.front();
            want.pop_front();
        }
        Net::Response res = Net::get(YT::thumbUrl(vid), {}, 15);
        std::lock_guard<std::mutex> l(mu);
        if (res.ok()) ready.emplace_back(vid, std::move(res.body));
        else requested.erase(vid);   // try again later
    }
}
