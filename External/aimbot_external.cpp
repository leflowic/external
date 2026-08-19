#include "aimbot_external.h"
#include "mem_external.h"
#include "offsets.h"
#include <cmath>

AIMBOT::Config AIMBOT::config;

static HANDLE g_hProc = nullptr;
static uintptr_t g_gtaBase = 0;
static uintptr_t g_sampBase = 0;

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

// Mirrors AIMBOT::getFov in SAMP_Internal/aimbot.cpp exactly.
static float getFov(Vector3 aimAngle, float camAlpha, float camBeta) {
    Vector3 ang, aim;
    angleVectors({ camAlpha, camBeta, 0 }, &aim);
    angleVectors(aimAngle, &ang);

    float dotProduct = ang.x * aim.x + ang.y * aim.y + ang.z * aim.z;
    float lengthSquared = aim.x * aim.x + aim.y * aim.y + aim.z * aim.z;

    return acosf(dotProduct / lengthSquared) * (180.f / (float)PI);
}

// Mirrors AIMBOT::calcAngle exactly.
static Vector3 calcAngle(Vector3 localPos, Vector3 camPos, Vector3 targetPos, Vector3 targetSpeed, int16_t camMode, float camFov) {
    Vector3 posTarget = targetPos;

    if (AIMBOT::config.prediction) {
        posTarget += targetSpeed * (float)AIMBOT::config.predictionLvl;
    }
    posTarget.z += AIMBOT::config.heightChange;

    Vector3 delta = { fabsf(camPos.x - posTarget.x), fabsf(camPos.y - posTarget.y), camPos.z - posTarget.z };
    float delta2DLength = sqrtf(delta.x * delta.x + delta.y * delta.y);
    float alpha = asinf(delta.x / delta2DLength);
    float beta = acosf(delta.x / delta2DLength);

    if ((localPos.x > posTarget.x) && (localPos.y < posTarget.y)) { beta = -beta; }
    if ((localPos.x > posTarget.x) && (localPos.y > posTarget.y)) { beta = beta; }
    if ((localPos.x < posTarget.x) && (localPos.y > posTarget.y)) { beta = (alpha + 1.5707f); }
    if ((localPos.x < posTarget.x) && (localPos.y < posTarget.y)) { beta = (-alpha - 1.5707f); }

    float pitch = atan2f(delta2DLength, delta.z) - 3.1415926f / 2.f;

    if (camMode == 53 || camMode == 55) {
        pitch -= camFov * 0.1f / 70.f;
        beta += camFov * 0.044f / 70.f;
    }

    return { pitch, beta, 0 };
}

void AIMBOT::init(HANDLE hProc, uintptr_t gtaBase, uintptr_t sampBase) {
    g_hProc = hProc;
    g_gtaBase = gtaBase;
    g_sampBase = sampBase;
}

void AIMBOT::tick() {
    if (!config.enabled) return;
    if (!(GetAsyncKeyState(config.aimKey) & 0x8000)) return;

    // Throttle to a fixed ~62Hz rather than the main loop's effectively
    // unthrottled ~1000Hz - no need to write the camera angle more often
    // than that.
    static ULONGLONG lastTick = 0;
    ULONGLONG now = GetTickCount64();
    if (now - lastTick < 16) return;
    lastTick = now;

    uintptr_t sampAddr = MEM::FindDMAAddy(g_hProc, g_sampBase + OFF::SAMP_PTR, { 0 });
    if (!sampAddr) return;

    int gamestate = MEM::Read<int>(g_hProc, sampAddr + OFF::SAMP_GAMESTATE);
    if (gamestate != (int)OFF::SAMP_GAMESTATE_CONNECTED) return;

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
    uint8_t localTeamID = MEM::Read<uint8_t>(g_hProc, localplayerAddr + OFF::LOCALPLAYER_TEAMID);

    uintptr_t camAddr = MEM::FindDMAAddy(g_hProc, g_gtaBase + OFF::CAMERA_PTR, { 0 }) - OFF::CAMERA_ADJUST;
    float camAlpha = MEM::Read<float>(g_hProc, camAddr + OFF::CAM_ALPHA);
    float camBeta = MEM::Read<float>(g_hProc, camAddr + OFF::CAM_BETA);
    float camFov = MEM::Read<float>(g_hProc, camAddr + OFF::CAM_FOV);
    int16_t camMode = MEM::Read<int16_t>(g_hProc, camAddr + OFF::CAM_MODE);
    Vector3 camPos = MEM::Read<Vector3>(g_hProc, camAddr + OFF::CAM_POS);

    uint32_t maxID = MEM::Read<uint32_t>(g_hProc, playerpoolAddr + OFF::POOL_MAXID);
    uint16_t localID = MEM::Read<uint16_t>(g_hProc, playerpoolAddr + OFF::POOL_LOCALID);

    float lowestFov = config.fov;
    bool found = false;
    Vector3 bestAngle{};

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
        if (!gtaPedAddr) continue;

        float hp = MEM::Read<float>(g_hProc, gtaPedAddr + OFF::ACTOR_HP);
        if (!(hp > 0)) continue;

        if (config.dontShootMates) {
            uint8_t teamID = MEM::Read<uint8_t>(g_hProc, dataAddr + OFF::RPDATA_TEAMID);
            if (teamID == localTeamID) continue;
        }

        uintptr_t maxAddr = MEM::Read<uint32_t>(g_hProc, gtaPedAddr + OFF::ACTOR_MAX);
        if (!maxAddr) continue;
        Vector3 targetPos = MEM::Read<Vector3>(g_hProc, maxAddr + OFF::MATRIX_POS);
        Vector3 targetSpeed = MEM::Read<Vector3>(g_hProc, gtaPedAddr + OFF::ACTOR_SPEED);

        Vector3 angle = calcAngle(localPos, camPos, targetPos, targetSpeed, camMode, camFov);
        float fov = getFov(angle, camAlpha, camBeta);

        if (fov < lowestFov) {
            lowestFov = fov;
            bestAngle = angle;
            found = true;
        }
    }

    if (!found) return;

    bestAngle.correctAngles();

    MEM::Write<float>(g_hProc, camAddr + OFF::CAM_ALPHA, bestAngle.x);
    MEM::Write<float>(g_hProc, camAddr + OFF::CAM_BETA, bestAngle.y);
}
