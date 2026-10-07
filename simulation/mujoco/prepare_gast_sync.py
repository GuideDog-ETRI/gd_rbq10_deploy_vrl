#!/usr/bin/env python3
"""Generate the GAST simulator source (MujocoGastSync) for any approved RBQ SDK.

GAST = the shared synchronized-camera overlay (prepare_sync_vision.py, next to this file)
plus one addition: every belly image header carries the capture-time world pose of base_link
(``/capture_sync_v1/gast_world_v1=x,y,yaw,qw,qx,qy,qz``), which the GAST student needs.
A copy frozen on the legacy SDK is kept in archive/legacy/sync_gast_source_legacy_sdk/.
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parent
COMMON = HERE / "prepare_sync_vision.py"
OUTPUT = HERE.parents[1] / "build/sync_gast_source"

POSE = """        publisher.setCaptureTimeNs(captureNs);
        const int gastBody=mj_name2id(Model,mjOBJ_BODY,"base_link");
        if (gastBody<0) throw std::runtime_error("GAST base_link missing");
        const auto* gp=captured->xpos+3*gastBody;
        const auto* gq=captured->xquat+4*gastBody;
        const double gyaw=std::atan2(2*(gq[0]*gq[3]+gq[1]*gq[2]),1-2*(gq[2]*gq[2]+gq[3]*gq[3]));
        char gastPose[256];
        std::snprintf(gastPose,sizeof(gastPose),"%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g",gp[0],gp[1],gyaw,gq[0],gq[1],gq[2],gq[3]);
        publisher.setWorldPose(gastPose);"""


# Evaluation-only external push (matches the v2 teacher's stair hip-handle disturbance). A UDP datagram on
# 127.0.0.1:19150 "fx fy fz px py pz seconds" applies world force F at base_link-frame point p for `seconds`
# of simulation time (xfrc_applied: F at the body CoM plus (p_world - CoM) x F). "0 0 0 0 0 0 0" cancels.
PUSH_GLOBALS = r"""
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
struct ExternalPush { double f[3]{}; double p[3]{}; double seconds = 0; bool pending = false; };
static std::mutex g_pushMtx;
static ExternalPush g_pushCmd;
static double g_pushEnd = -1, g_pushF[3]{}, g_pushP[3]{};
static int g_pushBody = -2;
static void PushListenerThread() {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(19150);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (fd < 0 || bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "[push] UDP 127.0.0.1:19150 unavailable; external push disabled" << std::endl; return;
    }
    std::cout << "[push] listening on 127.0.0.1:19150 (fx fy fz px py pz seconds)" << std::endl;
    char buf[256];
    while (true) {
        const ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) continue;
        buf[n] = 0;
        ExternalPush cmd;
        if (std::sscanf(buf, "%lf %lf %lf %lf %lf %lf %lf", &cmd.f[0], &cmd.f[1], &cmd.f[2],
                        &cmd.p[0], &cmd.p[1], &cmd.p[2], &cmd.seconds) == 7 && std::isfinite(cmd.seconds)) {
            cmd.pending = true;
            std::lock_guard<std::mutex> lock(g_pushMtx);
            g_pushCmd = cmd;
        }
    }
}
static void ApplyExternalPush(const mjModel* m, mjData* d) {
    if (g_pushBody == -2) g_pushBody = mj_name2id(m, mjOBJ_BODY, "base_link");
    if (g_pushBody < 0) return;
    {
        std::lock_guard<std::mutex> lock(g_pushMtx);
        if (g_pushCmd.pending) {
            g_pushCmd.pending = false;
            for (int i = 0; i < 3; ++i) { g_pushF[i] = g_pushCmd.f[i]; g_pushP[i] = g_pushCmd.p[i]; }
            g_pushEnd = g_pushCmd.seconds > 0 ? d->time + g_pushCmd.seconds : -1;
            std::cout << "[push] F=(" << g_pushF[0] << "," << g_pushF[1] << "," << g_pushF[2] << ") N at ("
                      << g_pushP[0] << "," << g_pushP[1] << "," << g_pushP[2] << ") for " << g_pushCmd.seconds
                      << " s, t=" << d->time << std::endl;
        }
    }
    mjtNum* wrench = d->xfrc_applied + 6 * g_pushBody;
    if (d->time >= g_pushEnd) {
        if (g_pushEnd >= 0) { mju_zero(wrench, 6); g_pushEnd = -1; std::cout << "[push] end t=" << d->time << std::endl; }
        return;
    }
    mjtNum local[3] = {g_pushP[0], g_pushP[1], g_pushP[2]}, world[3], arm[3], force[3] = {g_pushF[0], g_pushF[1], g_pushF[2]};
    mju_mulMatVec3(world, d->xmat + 9 * g_pushBody, local);
    for (int i = 0; i < 3; ++i) arm[i] = d->xpos[3 * g_pushBody + i] + world[i] - d->xipos[3 * g_pushBody + i];
    mju_copy3(wrench, force);
    mju_cross(wrench + 3, arm, force);
}
"""


def replace(text, old, new, count=1):
    if text.count(old) != count:
        raise RuntimeError(f"unexpected simulator source: {old[:60]!r} x{text.count(old)}, want {count}")
    return text.replace(old, new)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("vendor", type=Path, help="RBQ SDK root (RBQ-nightly)")
    args = parser.parse_args()
    subprocess.run([sys.executable, str(COMMON), str(args.vendor), str(OUTPUT)], check=True)
    main_cpp = OUTPUT / "src/main.cpp"
    text = replace(main_cpp.read_text(), "        publisher.setCaptureTimeNs(captureNs);", POSE)
    text = replace(text, "mjModel *Model  = nullptr;\n", "mjModel *Model  = nullptr;\n" + PUSH_GLOBALS)
    text = replace(text, "void PhysicsLoop(mujoco::Simulate* sim, const char* filename, const rbq_mujoco::RobotSpec &spec);",
                   "void PhysicsLoop(mujoco::Simulate* sim, const char* filename, const rbq_mujoco::RobotSpec &spec);\n"
                   "static void PushListenerThread();")
    text = replace(text, "mj_step(Model, Data);", "ApplyExternalPush(Model, Data);\n                        mj_step(Model, Data);", 2)
    text = replace(text, "    threads.push_back(new std::thread(PhysicsLoop, sim.get(), path.c_str(), spec));",
                   "    threads.push_back(new std::thread(PhysicsLoop, sim.get(), path.c_str(), spec));\n"
                   "    threads.push_back(new std::thread(PushListenerThread));")
    main_cpp.write_text(text)
    header = OUTPUT / "src/utils/VisionPublisher.h"
    text = replace(header.read_text(), "    void setCaptureTimeNs(int64_t ns) { m_captureNs = ns; }",
                   "    void setCaptureTimeNs(int64_t ns) { m_captureNs = ns; }\n"
                   "    void setWorldPose(const std::string& pose) { m_worldPose = pose; }")
    header.write_text(replace(text, "    int64_t m_captureNs = 0;", "    int64_t m_captureNs = 0;\n    std::string m_worldPose;"))
    source = OUTPUT / "src/utils/VisionPublisher.cpp"
    source.write_text(replace(source.read_text(), 'msg.header().frame_id() += "/capture_sync_v1";',
                              'msg.header().frame_id() += "/capture_sync_v1/gast_world_v1=" + m_worldPose;', 3))
    cmake = OUTPUT / "CMakeLists.txt"
    cmake.write_text(re.sub(r"\bMujocoVrlSync\b", "MujocoGastSync", cmake.read_text()))
    print(f"Generated GAST simulator source: {OUTPUT}")


if __name__ == "__main__":
    main()
