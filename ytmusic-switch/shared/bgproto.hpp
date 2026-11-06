#pragma once
// Protocol between the YT Music app and its background service (sysmodule).
// The app appends commands to ctl.json; the service polls it, plays, and reports in status.json.
// Plain files on the SD card: no IPC service to register, and it works the same on Linux for tests.
#include <string>

namespace BgProto {
constexpr unsigned long long kProgramId = 0x420000000059544DULL;   // "YTM"
constexpr const char *kProgramIdStr = "420000000059544D";
inline std::string dir(const std::string &root) { return root + "/bg"; }
inline std::string ctl(const std::string &root) { return dir(root) + "/ctl.json"; }
inline std::string status(const std::string &root) { return dir(root) + "/status.json"; }
// status.json counts as "alive" if written within this many seconds (the service rewrites it every second
// while the app is pinging, and at least every 20 s otherwise)
constexpr int kAliveSecs = 25;
}
