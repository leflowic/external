#include "triggerbot_external.h"
#include "mem_external.h"
#include "offsets.h"
#include "vector.h"
#include <cmath>

TRIGGERBOT::Config TRIGGERBOT::config;

static HANDLE g_hProc = nullptr;
static uintptr_t g_gtaBase = 0;
static uintptr_t g_sampBase = 0;

// Absolute address from the source CLEO script, corroborated independently by
// a second, separately-authored external tool (a .NET trigger bot whose IL
// also reads 0xB6F3B8) - two independent implementations agreeing on this
// address is much stronger evidence than either alone. GTA SA US 1.0 loads at
// a fixed 0x400000 (no ASLR), so "absolute - 0x400000" is the RVA to add to
// gtaBase, keeping this correct even if a given copy loads elsewhere.
//
// This field alone is only a soft lock-on (the CLEO script's "targeted_actor")
// - it stays set for anyone nearby/visible, not just whoever the crosshair is
// precisely on, which is why firing on it alone shot at anyone with a visible
// health bar. The original script paired it with a second "aiming_at_actor"
// check; here that's done by finding which SA-MP player this native CPed
// pointer actually belongs to and checking the crosshair is tightly on them,
// reusing the same angle math the aimbot already uses.
constexpr uint32_t NATIVE_TARGET_BASE_RVA = 0xB6F3B8 - 0x400000; // deref -> +0x79C -> pointer field address
constexpr uint32_t NATIVE_TARGET_FIELD_OFF = 0x79C;

void TRIGGERBOT::init(HANDLE hProc, uintptr_t gtaBase, uintptr_t sampBase) {
    g_hProc = hProc;
    g_gtaBase = gtaBase;
    g_sampBase = sampBase;
}

static void FireClick() {
    INPUT down{};
    down.type = INPUT_MOUSE;
    down.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    INPUT up{};
    up.type = INPUT_MOUSE;
    up.mi.dwFlags = MOUSEEVENTF_LEFTUP;

    SendInput(1, &down, sizeof(INPUT));
    Sleep(20);
    SendInput(1, &up, sizeof(INPUT));
}

constexpr double PI = 3.14159265358979323846;

static void sinCos(float radians, float* sine, float* cosine) { *sine = sinf(radians); *cosine = cosf(radians); }

static void angleVectors(Vector3 angles, Vector3* forward) {
    float sp, sy, cp, cy;
    sinCos(angles.y, &sy, &cy);
    sinCos(angles.x, &sp, &cp);
    forward->x = cp * cy;
    forward->y = cp * sy;
    forward->z = -sp;
}

static float getFov(Vector3 aimAngle, float camAlpha, float camBeta) {
    Vector3 ang, aim;
    angleVectors({ camAlpha, camBeta, 0 }, &aim);
    angleVectors(aimAngle, &ang);

    float dotProduct = ang.x * aim.x + ang.y * aim.y + ang.z * aim.z;
    float lengthSquared = aim.x * aim.x + aim.y * aim.y + aim.z * aim.z;

    return acosf(dotProduct / lengthSquared) * (180.f / (float)PI);
}

// Same shape as AIMBOT's calcAngle, minus prediction/height offset.
static Vector3 angleToTarget(Vector3 localPos, Vector3 camPos, Vector3 targetPos) {
    Vector3 delta = { fabsf(camPos.x - targetPos.x), fabsf(camPos.y - targetPos.y), camPos.z - targetPos.z };
    float delta2DLength = sqrtf(delta.x * delta.x + delta.y * delta.y);
    float alpha = asinf(delta.x / delta2DLength);
    float beta = acosf(delta.x / delta2DLength);

    if ((localPos.x > targetPos.x) && (localPos.y < targetPos.y)) { beta = -beta; }
    if ((localPos.x > targetPos.x) && (localPos.y > targetPos.y)) { beta = beta; }
    if ((localPos.x < targetPos.x) && (localPos.y > targetPos.y)) { beta = (alpha + 1.5707f); }
    if ((localPos.x < targetPos.x) && (localPos.y < targetPos.y)) { beta = (-alpha - 1.5707f); }

    float pitch = atan2f(delta2DLength, delta.z) - 3.1415926f / 2.f;
    return { pitch, beta, 0 };
}

void TRIGGERBOT::tick() {
    if (!config.enabled) return;
    if (!(GetAsyncKeyState(config.key) & 0x8000)) return;

    static ULONGLONG lastShot = 0;
    ULONGLONG now = GetTickCount64();
    if (now - lastShot < (ULONGLONG)config.holdMs) return;

    uintptr_t sampAddr = MEM::FindDMAAddy(g_hProc, g_sampBase + OFF::SAMP_PTR, { 0 });
    if (!sampAddr) return;

    uintptr_t poolsAddr = MEM::Read<uint32_t>(g_hProc, sampAddr + OFF::SAMP_POOLS);
    if (!poolsAddr) return;
    uintptr_t playerpoolAddr = MEM::Read<uint32_t>(g_hProc, poolsAddr + OFF::POOLS_PLAYERPOOL);
    if (!playerpoolAddr) return;

    uintptr_t localplayerAddr = MEM::Read<uint32_t>(g_hProc, playerpoolAddr + OFF::POOL_LOCALPLAYER);
    if (!localplayerAddr) return;
    uintptr_t localActorAddr = MEM::Read<uint32_t>(g_hProc, localplayerAddr + OFF::LOCALPLAYER_ACTOR);
    if (!localActorAddr) return;
    uintptr_t localGtaPedAddr = MEM::Read<uint32_t>(g_hProc, localActorAddr + OFF::PED_GTAPED);
    if (!localGtaPedAddr) return;

    uint32_t localState = MEM::Read<uint32_t>(g_hProc, localGtaPedAddr + OFF::ACTOR_STATE);
    if (localState == OFF::ACTOR_STATE_DYING || localState == OFF::ACTOR_STATE_DEAD) return;

    uintptr_t localMaxAddr = MEM::Read<uint32_t>(g_hProc, localGtaPedAddr + OFF::ACTOR_MAX);
    if (!localMaxAddr) return;
    Vector3 localPos = MEM::Read<Vector3>(g_hProc, localMaxAddr + OFF::MATRIX_POS);

    // Native soft-lock: is there ANY nearby/visible target at all right now?
    uintptr_t targetFieldAddr = MEM::FindDMAAddy(g_hProc, g_gtaBase + NATIVE_TARGET_BASE_RVA, { NATIVE_TARGET_FIELD_OFF });
    if (!targetFieldAddr) return;
    uint32_t targetedActor = MEM::Read<uint32_t>(g_hProc, targetFieldAddr);
    if (!(targetedActor > 0)) return;

    uintptr_t camAddr = MEM::FindDMAAddy(g_hProc, g_gtaBase + OFF::CAMERA_PTR, { 0 }) - OFF::CAMERA_ADJUST;
    float camAlpha = MEM::Read<float>(g_hProc, camAddr + OFF::CAM_ALPHA);
    float camBeta = MEM::Read<float>(g_hProc, camAddr + OFF::CAM_BETA);
    Vector3 camPos = MEM::Read<Vector3>(g_hProc, camAddr + OFF::CAM_POS);

    uint32_t maxID = MEM::Read<uint32_t>(g_hProc, playerpoolAddr + OFF::POOL_MAXID);
    uint16_t localID = MEM::Read<uint16_t>(g_hProc, playerpoolAddr + OFF::POOL_LOCALID);

    // Find which SA-MP player the native lock actually points at, then
    // require the crosshair to be tightly on THAT one before firing.
    for (uint32_t i = 0; i <= maxID && i < (uint32_t)OFF::SAMP_MAX_PLAYERS; ++i) {
        if (i == localID) continue;

        uintptr_t remoteplayerAddr = MEM::Read<uint32_t>(g_hProc, playerpoolAddr + OFF::POOL_REMOTEPTRS + i * 4);
        if (!remoteplayerAddr) continue;

        int32_t isListed = MEM::Read<int32_t>(g_hProc, playerpoolAddr + OFF::POOL_ISLISTED + i * 4);
        if (!isListed) continue;

        uintptr_t dataAddr = MEM::Read<uint32_t>(g_hProc, remoteplayerAddr + OFF::RP_DATA);
        if (!dataAddr) continue;

        uintptr_t actorAddr = MEM::Read<uint32_t>(g_hProc, dataAddr + OFF::RPDATA_ACTOR);
        if (!actorAddr) continue;

        uintptr_t gtaPedAddr = MEM::Read<uint32_t>(g_hProc, actorAddr + OFF::PED_GTAPED);
        if (gtaPedAddr != targetedActor) continue; // not the locked-on player

        float hp = MEM::Read<float>(g_hProc, gtaPedAddr + OFF::ACTOR_HP);
        if (!(hp > 0)) return;

        uintptr_t maxAddr = MEM::Read<uint32_t>(g_hProc, gtaPedAddr + OFF::ACTOR_MAX);
        if (!maxAddr) return;
        Vector3 targetPos = MEM::Read<Vector3>(g_hProc, maxAddr + OFF::MATRIX_POS);

        Vector3 angle = angleToTarget(localPos, camPos, targetPos);
        float fov = getFov(angle, camAlpha, camBeta);

        if (fov <= config.fov) {
            FireClick();
            lastShot = now;
        }
        return;
    }
}
