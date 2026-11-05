// CI smoke test: links every library the app will use.
#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <curl/curl.h>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}
int main() {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    TTF_Init();
    IMG_Init(IMG_INIT_JPG);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    printf("%s %u\n", avcodec_configuration(), swresample_version());
    const AVCodec *c = avcodec_find_decoder(AV_CODEC_ID_AAC);
    const AVCodec *o = avcodec_find_decoder(AV_CODEC_ID_OPUS);
    printf("aac=%d opus=%d demux=%d\n", c != nullptr, o != nullptr, av_find_input_format("mp4") != nullptr);
    SDL_Quit();
    return 0;
}
