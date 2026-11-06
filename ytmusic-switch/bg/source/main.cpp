// ytmusic-bg: YT Music's background player (Atmosphère sysmodule). Started by the app via pm:shell.
#include "engine.hpp"
#include "service.hpp"
#include "../../shared/bgproto.hpp"
#include "../../source/net.hpp"
#include <switch.h>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <sys/stat.h>

extern "C" {
u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;

// 8 MB heap: curl/TLS, ffmpeg's Opus/AAC decoders and small buffers. Songs stream through a file on the SD card.
#define HEAP_SIZE (8 * 1024 * 1024)
static char g_heap[HEAP_SIZE] __attribute__((aligned(0x1000)));
void __libnx_initheap(void) {
    extern char *fake_heap_start;
    extern char *fake_heap_end;
    fake_heap_start = g_heap;
    fake_heap_end = g_heap + HEAP_SIZE;
}

void __libnx_init_time(void);

void __appInit(void) {
    if (R_FAILED(smInitialize())) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));
    if (R_SUCCEEDED(setsysInitialize())) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }
    fsInitialize();
    fsdevMountSdmc();
    timeInitialize();
    __libnx_init_time();
    static const SocketInitConfig sockCfg = {
        .tcp_tx_buf_size = 0x8000,
        .tcp_rx_buf_size = 0x10000,
        .tcp_tx_buf_max_size = 0x20000,
        .tcp_rx_buf_max_size = 0x40000,
        .udp_tx_buf_size = 0x2400,
        .udp_rx_buf_size = 0xA500,
        .sb_efficiency = 2,
        .num_bsd_sessions = 3,
        .bsd_service_type = BsdServiceType_Auto,
    };
    socketInitialize(&sockCfg);
    smExit();
}

void __appExit(void) {
    socketExit();
    timeExit();
    fsdevUnmountAll();
    fsExit();
}
}

static const char *kRoot = "sdmc:/switch/ytmusic";

void bglog(const char *fmt, ...) {
    static FILE *f = nullptr;
    if (!f) {
        f = fopen((std::string(kRoot) + "/bg/log.txt").c_str(), "w");
        if (!f) return;
    }
    va_list ap;
    va_start(ap, fmt);
    fprintf(f, "[%lld] ", (long long)time(nullptr));
    vfprintf(f, fmt, ap);
    fputc('\n', f);
    va_end(ap);
    fflush(f);
}

Sink *makeAudrenSink();

int main() {
    mkdir(kRoot, 0777);
    mkdir((std::string(kRoot) + "/bg").c_str(), 0777);
    bglog("ytmusic-bg started");
    Net::init();
    Sink *sink = makeAudrenSink();
    if (!sink->open()) { bglog("audio renderer failed"); return 1; }
    {
        Engine engine(sink, kRoot);
        BgService service(engine, kRoot);
        while (!service.quitRequested()) {
            service.poll();
            svcSleepThread(100'000'000);
        }
    }
    sink->close();
    delete sink;
    Net::cleanup();
    bglog("ytmusic-bg stopped");
    return 0;
}
