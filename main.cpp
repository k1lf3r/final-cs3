// ============================================================================
//  CS2 OPENGL FINAL PROJECT (FIXED)
//  de_aztec (built from the radar image), Buy Menu, C4, Teams, Bots, Rounds
//  OpenGL 3.3 Core, GLFW, GLAD, GLM
//
//  What was fixed / added compared to the old version:
//   * Real physics: floor detection, steps, jumping on crates, gravity.
//     No more "floating" and no more falling out of the map.
//   * Death: HP <= 0 -> death camera, "YOU DIED", respawn / next round.
//   * Bitmap font: all menu / buy menu / HUD text is now visible.
//   * Mouse coordinates and viewport work on HiDPI screens.
//   * de_aztec is generated from the radar rectangles (see AZTEC_RECTS).
//   * Bots: line of sight, chase, strafe, wander, real humanoid model.
//   * Round system: bomb plant/defuse/explosion, scores, timer, minimap.
// ============================================================================

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <vector>
#include <string>
#include <iostream>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cctype>
#include <ctime>
#include <algorithm>

// ----------------------------------------------------------------------------
// Window & States
// ----------------------------------------------------------------------------
int g_winW = 1280, g_winH = 720;   // window size (screen coords) -> UI + mouse
int g_fbW = 1280, g_fbH = 720;     // framebuffer size (pixels)   -> glViewport
double g_mouseX = 0, g_mouseY = 0;

enum GameState { STATE_MAIN_MENU, STATE_TEAM_SELECT, STATE_PLAYING, STATE_PAUSE_MENU, STATE_BUY_MENU };
enum Team { TEAM_NONE, TEAM_T, TEAM_CT };
enum MapID { MAP_AZTEC, MAP_TRAINING };

GameState g_state = STATE_MAIN_MENU;
Team      g_playerTeam = TEAM_T;
MapID     g_currentMap = MAP_AZTEC;
bool      g_training = false;

// ----------------------------------------------------------------------------
// Weapons & Inventory
// ----------------------------------------------------------------------------
enum WeaponType {
    WEAPON_KNIFE, WEAPON_GLOCK, WEAPON_USP, WEAPON_DEAGLE,
    WEAPON_AK47, WEAPON_M4A1, WEAPON_AWP, WEAPON_C4, WEAPON_COUNT
};
enum SlotId { SLOT_PRIMARY = 0, SLOT_SECONDARY = 1, SLOT_KNIFE = 2, SLOT_C4 = 3, SLOT_COUNT = 4 };

struct WeaponInfo {
    const char* name;
    int   price, maxClip, maxReserve;
    float fireRate, reloadTime, damage;
    bool  isAutomatic, isSniper;
    int   slot;
    Team  team;
};

const WeaponInfo WEAPON_DB[WEAPON_COUNT] = {
    { "KNIFE",        0,    0,  0,   0.50f, 0.0f, 40.0f,  false, false, SLOT_KNIFE,     TEAM_NONE },
    { "GLOCK-18",     200,  20, 120, 0.15f, 1.8f, 22.0f,  false, false, SLOT_SECONDARY, TEAM_T    },
    { "USP-S",        200,  12, 24,  0.16f, 1.9f, 26.0f,  false, false, SLOT_SECONDARY, TEAM_CT   },
    { "DESERT EAGLE", 700,  7,  35,  0.28f, 2.2f, 55.0f,  false, false, SLOT_SECONDARY, TEAM_NONE },
    { "AK-47",        2700, 30, 90,  0.10f, 2.3f, 36.0f,  true,  false, SLOT_PRIMARY,   TEAM_T    },
    { "M4A1",         2900, 30, 90,  0.09f, 2.4f, 33.0f,  true,  false, SLOT_PRIMARY,   TEAM_CT   },
    { "AWP",          4750, 5,  30,  1.10f, 3.2f, 115.0f, false, true,  SLOT_PRIMARY,   TEAM_NONE },
    { "C4",           0,    1,  0,   1.00f, 0.0f, 0.0f,   false, false, SLOT_C4,        TEAM_T    }
};

struct WeaponSlot { WeaponType type; int clip; int reserve; bool has; };
WeaponSlot g_inv[SLOT_COUNT];
int g_activeSlot = 0;

int g_money = 10000, g_armor = 100, g_health = 100;
int g_kills = 0, g_deaths = 0, g_scoreT = 0, g_scoreCT = 0;

float g_shotCooldown = 0, g_reloadTimer = 0, g_recoilTimer = 0, g_hitMarkerTimer = 0;
float g_damageFlash = 0, g_tracerTimer = 0;
glm::vec3 g_tracerStart(0), g_tracerEnd(0);
bool  g_scoped = false;

bool  g_dead = false;
float g_deathTimer = 0;

std::string g_msg; float g_msgTimer = 0;
std::string g_banner;
std::string g_hint;
void showMsg(const std::string& s) { g_msg = s; g_msgTimer = 2.5f; }

void giveWeapon(WeaponType t) {
    const WeaponInfo& w = WEAPON_DB[t];
    g_inv[w.slot] = { t, w.maxClip, w.maxReserve, true };
}

void equipDefaultWeapons() {
    for (int i = 0; i < SLOT_COUNT; i++) g_inv[i] = { WEAPON_KNIFE, 0, 0, false };
    giveWeapon(WEAPON_KNIFE);
    if (g_playerTeam == TEAM_T) {
        giveWeapon(WEAPON_GLOCK);
        giveWeapon(WEAPON_AK47);
        giveWeapon(WEAPON_C4);
    } else {
        giveWeapon(WEAPON_USP);
        giveWeapon(WEAPON_M4A1);
    }
    g_activeSlot = SLOT_PRIMARY;
}

void ensureActiveValid() {
    if (g_inv[g_activeSlot].has) return;
    for (int i = 0; i < SLOT_COUNT; i++) if (g_inv[i].has) { g_activeSlot = i; return; }
}

// ----------------------------------------------------------------------------
// Camera / Player physics
// ----------------------------------------------------------------------------
glm::vec3 g_feet(0.0f), g_vel(0.0f);        // g_feet = position of the player's FEET
float yaw = 0.0f, pitch = 0.0f;
float mouseSensitivity = 0.12f;
bool  firstMouse = true;
double lastX = 0, lastY = 0;

const float EYE_STAND = 1.65f;
const float EYE_CROUCH = 1.05f;
float currentEyeHeight = EYE_STAND;
bool  isCrouching = false;
bool  g_grounded = false;

const float GRAVITY = -24.0f;
const float JUMP_SPEED = 8.0f;
const float PLAYER_R = 0.45f;
const float PLAYER_H = 1.8f;
const float STEP_H = 0.75f;    // max step the player can walk up

glm::vec3 getFront() {
    glm::vec3 f;
    f.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    f.y = sin(glm::radians(pitch));
    f.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    return glm::normalize(f);
}
glm::vec3 eyePos() { return g_feet + glm::vec3(0.0f, currentEyeHeight, 0.0f); }

// ----------------------------------------------------------------------------
// World geometry
// ----------------------------------------------------------------------------
struct WorldBox {
    glm::vec3 center, halfSize, color;
    bool collidable, isWater;
};
std::vector<WorldBox> g_world;

struct SiteArea {
    char letter;
    glm::vec3 minAABB, maxAABB, markerPos;
};
std::vector<SiteArea> g_bombSites;

void addBox(glm::vec3 c, glm::vec3 h, glm::vec3 col, bool collidable = true, bool water = false) {
    WorldBox b; b.center = c; b.halfSize = h; b.color = col; b.collidable = collidable; b.isWater = water;
    g_world.push_back(b);
}

// Highest walkable surface under (x,z) that is not higher than maxTop
float groundAt(const glm::vec3& p, float r, float maxTop) {
    float best = -1000.0f;
    for (const auto& b : g_world) {
        if (!b.collidable) continue;
        float top = b.center.y + b.halfSize.y;
        if (top > maxTop || top <= best) continue;
        if (p.x + r > b.center.x - b.halfSize.x && p.x - r < b.center.x + b.halfSize.x &&
            p.z + r > b.center.z - b.halfSize.z && p.z - r < b.center.z + b.halfSize.z)
            best = top;
    }
    return best;
}

// Push a vertical cylinder out of every box that really blocks it
void resolveXZ(glm::vec3& pos, float r, float h) {
    for (int it = 0; it < 2; it++) {
        for (const auto& b : g_world) {
            if (!b.collidable) continue;
            float top = b.center.y + b.halfSize.y, bot = b.center.y - b.halfSize.y;
            if (top <= pos.y + STEP_H) continue;     // can be stepped on -> not a wall
            if (bot >= pos.y + h) continue;          // above our head
            float minX = b.center.x - b.halfSize.x, maxX = b.center.x + b.halfSize.x;
            float minZ = b.center.z - b.halfSize.z, maxZ = b.center.z + b.halfSize.z;
            if (pos.x < minX - r || pos.x > maxX + r || pos.z < minZ - r || pos.z > maxZ + r) continue;
            float cx = glm::clamp(pos.x, minX, maxX), cz = glm::clamp(pos.z, minZ, maxZ);
            float dx = pos.x - cx, dz = pos.z - cz;
            float d2 = dx * dx + dz * dz;
            if (d2 >= r * r) continue;
            if (d2 > 1e-8f) {
                float d = sqrt(d2), push = r - d;
                pos.x += dx / d * push; pos.z += dz / d * push;
            } else {   // centre is inside the box -> leave through the nearest face
                float l = pos.x - minX, rr = maxX - pos.x, t = pos.z - minZ, bb = maxZ - pos.z;
                float m = std::min(std::min(l, rr), std::min(t, bb));
                if (m == l) pos.x = minX - r; else if (m == rr) pos.x = maxX + r;
                else if (m == t) pos.z = minZ - r; else pos.z = maxZ + r;
            }
        }
    }
}

bool rayAABB(glm::vec3 ro, glm::vec3 rd, glm::vec3 bmin, glm::vec3 bmax, float& tHit) {
    float tmin = 0.0001f, tmax = 1e9f;
    for (int i = 0; i < 3; i++) {
        if (fabs(rd[i]) < 1e-8f) {
            if (ro[i] < bmin[i] || ro[i] > bmax[i]) return false;
            continue;
        }
        float invD = 1.0f / rd[i];
        float t0 = (bmin[i] - ro[i]) * invD, t1 = (bmax[i] - ro[i]) * invD;
        if (invD < 0.0f) std::swap(t0, t1);
        tmin = std::max(tmin, t0); tmax = std::min(tmax, t1);
        if (tmax <= tmin) return false;
    }
    tHit = tmin;
    return true;
}

// Distance to the nearest solid thing along the ray (1e9 if nothing within maxT)
float raycastWorld(glm::vec3 ro, glm::vec3 rd, float maxT) {
    float best = 1e9f;
    for (const auto& b : g_world) {
        if (!b.collidable) continue;
        float t;
        if (rayAABB(ro, rd, b.center - b.halfSize, b.center + b.halfSize, t) && t < best && t <= maxT) best = t;
    }
    return best;
}

// ----------------------------------------------------------------------------
// de_aztec - built from the radar image (1024x768 px)
//   Every rectangle below is a walkable room/corridor in RADAR PIXELS.
//   Walls are generated automatically around them.
// ----------------------------------------------------------------------------
const float MAP_S = 0.13f;               // world units per radar pixel
const float RADAR_CX = 512.0f, RADAR_CZ = 384.0f;
inline float pxX(float px) { return (px - RADAR_CX) * MAP_S; }
inline float pxZ(float py) { return (py - RADAR_CZ) * MAP_S; }
inline float worldToPxX(float x) { return x / MAP_S + RADAR_CX; }
inline float worldToPxZ(float z) { return z / MAP_S + RADAR_CZ; }

enum FloorType { F_STONE, F_GRASS, F_DARK, F_LOW };
struct RadarRect { int x0, y0, x1, y1, type; };

const RadarRect AZTEC_RECTS[] = {
    // --- top-left: stairs area + Site A room
    {  48, 136, 112, 248, F_GRASS },
    { 112, 136, 160, 248, F_STONE },
    { 160, 136, 272, 272, F_STONE },          // SITE A
    // --- left side
    {   8, 248, 120, 384, F_GRASS },          // left jungle
    {   8, 384, 120, 592, F_STONE },          // T spawn room
    { 120, 512, 160, 592, F_STONE },
    // --- bottom tunnel to B
    { 160, 520, 320, 592, F_DARK },
    { 320, 520, 384, 632, F_DARK },
    { 384, 584, 400, 632, F_DARK },
    // --- B room
    { 400, 440, 512, 576, F_LOW },            // sunken mossy pit
    { 512, 440, 624, 640, F_STONE },
    { 400, 576, 512, 640, F_STONE },          // SITE B
    { 552, 392, 624, 440, F_STONE },          // strip going north
    { 384, 400, 552, 440, F_DARK },           // corridor
    // --- left-middle room
    { 168, 320, 272, 464, F_STONE },
    { 272, 352, 384, 440, F_STONE },
    { 208, 272, 248, 320, F_DARK },           // A <-> left-middle
    // --- A east exit and dark rooms
    { 272, 232, 296, 256, F_DARK },
    { 296, 200, 384, 256, F_DARK },
    { 296, 256, 336, 336, F_DARK },
    { 312, 336, 344, 352, F_DARK },
    { 384, 224, 400, 248, F_DARK },
    // --- centre (river / bridge)
    { 400, 192, 536, 256, F_LOW },
    { 400, 256, 520, 400, F_LOW },
    { 536, 200, 632, 264, F_DARK },
    // --- upper middle
    { 632, 176, 752, 280, F_GRASS },
    { 552, 280, 776, 352, F_STONE },
    { 568, 352, 616, 392, F_DARK },
    // --- north-east tunnels
    { 752, 200, 896, 264, F_DARK },
    { 776, 176, 832, 200, F_DARK },
    { 832, 112, 896, 200, F_DARK },
    { 896, 216, 968, 248, F_DARK },
    { 920, 112, 1000, 144, F_DARK },
    { 968, 144, 1000, 264, F_DARK },
    // --- east side
    { 776, 304, 800, 352, F_DARK },
    { 800, 296, 848, 424, F_STONE },
    { 848, 376, 904, 424, F_DARK },
    { 904, 264, 1016, 424, F_STONE }          // CT spawn room
};

const int CELL = 8, GRID_W = 128, GRID_H = 96;
bool g_grid[GRID_H][GRID_W];
std::vector<glm::vec3> g_miniRuns;            // x, y, width (radar px) of walkable runs

int snap8(int v) { return ((v + 4) / 8) * 8; }

void addCrate(float px, float py, float size, float floorY, float height, glm::vec3 col) {
    addBox(glm::vec3(pxX(px), floorY + height * 0.5f, pxZ(py)), glm::vec3(size * 0.5f, height * 0.5f, size * 0.5f), col);
}
void addDecal(int x0, int y0, int x1, int y1, glm::vec3 col) {
    addBox(glm::vec3((pxX((float)x0) + pxX((float)x1)) * 0.5f, 0.02f, (pxZ((float)y0) + pxZ((float)y1)) * 0.5f),
           glm::vec3((pxX((float)x1) - pxX((float)x0)) * 0.5f, 0.02f, (pxZ((float)y1) - pxZ((float)y0)) * 0.5f), col, false);
}

void buildWorldAztec() {
    g_world.clear(); g_bombSites.clear(); g_miniRuns.clear();
    memset(g_grid, 0, sizeof(g_grid));

    const glm::vec3 cStone(0.58f, 0.56f, 0.51f), cGrass(0.23f, 0.34f, 0.19f);
    const glm::vec3 cDark(0.36f, 0.36f, 0.34f), cLow(0.18f, 0.26f, 0.20f);
    const glm::vec3 crate1(0.62f, 0.50f, 0.32f), crate2(0.55f, 0.44f, 0.27f), wood(0.40f, 0.27f, 0.15f);

    // 1) floors
    for (const auto& r : AZTEC_RECTS) {
        int x0 = snap8(r.x0), y0 = snap8(r.y0), x1 = snap8(r.x1), y1 = snap8(r.y1);
        for (int j = y0 / CELL; j < y1 / CELL; j++)
            for (int i = x0 / CELL; i < x1 / CELL; i++)
                if (i >= 0 && i < GRID_W && j >= 0 && j < GRID_H) g_grid[j][i] = true;

        float fy = (r.type == F_LOW) ? -0.6f : 0.0f;
        glm::vec3 col = r.type == F_STONE ? cStone : r.type == F_GRASS ? cGrass : r.type == F_DARK ? cDark : cLow;
        float wx0 = pxX((float)x0), wx1 = pxX((float)x1), wz0 = pxZ((float)y0), wz1 = pxZ((float)y1);
        addBox(glm::vec3((wx0 + wx1) * 0.5f, fy - 0.5f, (wz0 + wz1) * 0.5f),
               glm::vec3((wx1 - wx0) * 0.5f, 0.5f, (wz1 - wz0) * 0.5f), col);
        if (r.type == F_LOW)   // shallow water over the low areas
            addBox(glm::vec3((wx0 + wx1) * 0.5f, -0.25f, (wz0 + wz1) * 0.5f),
                   glm::vec3((wx1 - wx0) * 0.5f, 0.05f, (wz1 - wz0) * 0.5f), glm::vec3(0.12f, 0.30f, 0.32f), false, true);
    }

    // 2) walls: one-cell thick ring around every walkable cell (8-neighbourhood -> no corner holes)
    auto isWalk = [&](int i, int j) { return i >= 0 && i < GRID_W && j >= 0 && j < GRID_H && g_grid[j][i]; };
    auto isBorder = [&](int i, int j) {
        if (isWalk(i, j)) return false;
        for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) if (isWalk(i + di, j + dj)) return true;
        return false;
    };
    for (int j = 0; j < GRID_H; j++) {
        int i = 0;
        while (i < GRID_W) {
            if (!isBorder(i, j)) { i++; continue; }
            int s = i;
            while (i < GRID_W && isBorder(i, j)) i++;
            float x0 = pxX((float)(s * CELL)), x1 = pxX((float)(i * CELL));
            float z0 = pxZ((float)(j * CELL)), z1 = pxZ((float)((j + 1) * CELL));
            float v = 0.85f + 0.15f * (float)((s * 7 + j * 13) % 5) / 4.0f;
            addBox(glm::vec3((x0 + x1) * 0.5f, 1.75f, (z0 + z1) * 0.5f),
                   glm::vec3((x1 - x0) * 0.5f, 4.75f, (z1 - z0) * 0.5f), glm::vec3(0.46f, 0.44f, 0.40f) * v);
        }
    }

    // 3) minimap runs
    for (int j = 0; j < GRID_H; j++) {
        int i = 0;
        while (i < GRID_W) {
            if (!g_grid[j][i]) { i++; continue; }
            int s = i;
            while (i < GRID_W && g_grid[j][i]) i++;
            g_miniRuns.push_back(glm::vec3((float)(s * CELL), (float)(j * CELL), (float)((i - s) * CELL)));
        }
    }

    // 4) wooden bridge over the river (centre top)
    addBox(glm::vec3(pxX(468), -0.05f, pxZ(236)), glm::vec3((pxX(536) - pxX(400)) * 0.5f, 0.1f, (pxZ(248) - pxZ(224)) * 0.5f), wood);
    addBox(glm::vec3(pxX(468), 0.55f, pxZ(224)), glm::vec3((pxX(536) - pxX(400)) * 0.5f, 0.5f, 0.08f), wood);
    addBox(glm::vec3(pxX(468), 0.55f, pxZ(248)), glm::vec3((pxX(536) - pxX(400)) * 0.5f, 0.5f, 0.08f), wood);

    // 5) decals
    addDecal(920, 280, 1000, 376, cGrass);        // grass patch in CT spawn

    // 6) crates (positions taken from the radar boxes)
    // Site A
    addCrate(250, 166, 2.0f, 0, 1.8f, crate1);
    addCrate(182, 242, 2.2f, 0, 1.4f, crate2);
    addCrate(232, 232, 1.4f, 0, 1.0f, crate1);
    // left jungle + T spawn
    addCrate(104, 306, 1.4f, 0, 1.0f, crate2);
    addCrate(104, 354, 1.4f, 0, 1.0f, crate1);
    addCrate(25, 547, 2.2f, 0, 1.4f, crate1);
    addCrate(93, 441, 1.4f, 0, 1.0f, crate2);
    addCrate(92, 483, 1.4f, 0, 1.0f, crate1);
    addCrate(126, 540, 1.2f, 0, 0.9f, crate2);
    // left-middle room
    addCrate(285, 372, 1.4f, 0, 1.0f, crate1);
    addCrate(352, 372, 1.4f, 0, 1.0f, crate2);
    addCrate(184, 362, 1.4f, 0, 1.4f, crate1);
    addCrate(184, 416, 1.4f, 0, 1.4f, crate2);
    // Site B + strip
    addCrate(608, 410, 1.8f, 0, 1.4f, crate1);
    addCrate(612, 437, 1.4f, 0, 1.0f, crate2);
    addCrate(565, 470, 1.4f, 0, 1.0f, crate1);
    addCrate(482, 600, 1.4f, 0, 1.0f, crate2);
    addCrate(601, 625, 1.8f, 0, 1.4f, crate1);
    addCrate(540, 610, 2.0f, 0, 1.6f, crate2);
    // upper middle
    addCrate(680, 308, 1.4f, 0, 1.0f, crate2);
    addCrate(745, 340, 1.4f, 0, 1.0f, crate1);
    addCrate(572, 296, 1.4f, 0, 1.0f, crate2);
    // CT spawn L-shaped box
    addCrate(968, 372, 2.0f, 0, 1.2f, crate1);
    addCrate(992, 372, 1.4f, 0, 1.8f, crate2);

    // 7) bombsites (A = top-left room, B = bottom room - where the red dots are on the radar)
    g_bombSites.push_back({ 'A', glm::vec3(pxX(168), -1, pxZ(144)), glm::vec3(pxX(264), 6, pxZ(264)), glm::vec3(pxX(216), 2.6f, pxZ(200)) });
    g_bombSites.push_back({ 'B', glm::vec3(pxX(416), -1, pxZ(520)), glm::vec3(pxX(616), 6, pxZ(636)), glm::vec3(pxX(478), 2.6f, pxZ(600)) });
}

void buildWorldTraining() {
    g_world.clear(); g_bombSites.clear(); g_miniRuns.clear();
    glm::vec3 wall(0.5f, 0.47f, 0.42f);
    addBox(glm::vec3(0, -0.5f, 0), glm::vec3(70, 0.5f, 90), glm::vec3(0.72f, 0.68f, 0.52f));
    addBox(glm::vec3(-70, 10, 0), glm::vec3(5, 13, 95), wall);
    addBox(glm::vec3(70, 10, 0), glm::vec3(5, 13, 95), wall);
    addBox(glm::vec3(0, 10, -95), glm::vec3(75, 13, 5), wall);
    addBox(glm::vec3(0, 10, 95), glm::vec3(75, 13, 5), wall);
    addBox(glm::vec3(0, 2, -40), glm::vec3(16, 2, 16), glm::vec3(0.78f, 0.65f, 0.40f));   // platform, top y = 4
    for (int k = 1; k <= 5; k++) {                                                          // stairs (step 0.7)
        float top = 0.7f * k;
        addBox(glm::vec3(0, top * 0.5f, -23.4f + (5 - k) * 1.2f), glm::vec3(3, top * 0.5f, 0.6f), glm::vec3(0.7f, 0.6f, 0.4f));
    }
    addBox(glm::vec3(-25, 0.75f, 0), glm::vec3(2, 0.75f, 2), glm::vec3(0.52f, 0.38f, 0.22f));
    addBox(glm::vec3(25, 0.75f, -10), glm::vec3(2, 0.75f, 2), glm::vec3(0.52f, 0.38f, 0.22f));
    g_bombSites.push_back({ 'A', glm::vec3(-15, 0, -56), glm::vec3(15, 6, -24), glm::vec3(0, 6.0f, -40) });
}

glm::vec3 spawnPos(Team t) {
    float x, z;
    if (g_currentMap == MAP_AZTEC) {
        if (t == TEAM_T) { x = pxX(64); z = pxZ(480); } else { x = pxX(960); z = pxZ(344); }
    } else { x = 0.0f; z = 45.0f; }
    float y = groundAt(glm::vec3(x, 0, z), 0.3f, 3.0f);
    if (y < -500.0f) y = 0.0f;
    return glm::vec3(x, y, z);
}

// ----------------------------------------------------------------------------
// Bots
// ----------------------------------------------------------------------------
struct Bot {
    glm::vec3 pos, home;           // pos = feet
    glm::vec2 wanderDir;
    float yaw, health, shootTimer, reactTimer, wanderTimer, respawnTimer, walkPhase, flash, strafe;
    bool  alive, moving;
    Team  team;
};
std::vector<Bot> g_bots;

Bot makeBot(glm::vec3 p, Team t, float y) {
    Bot b;
    b.pos = p; b.home = p; b.wanderDir = glm::vec2(0); b.yaw = y; b.health = 100.0f;
    b.shootTimer = 1.0f; b.reactTimer = 0.45f; b.wanderTimer = 0; b.respawnTimer = 0;
    b.walkPhase = 0; b.flash = 0; b.strafe = 1.0f; b.alive = true; b.moving = false; b.team = t;
    return b;
}
glm::vec3 floorPoint(float x, float z) {
    float y = groundAt(glm::vec3(x, 0, z), 0.3f, 3.0f);
    if (y < -500.0f) y = 0.0f;
    return glm::vec3(x, y, z);
}

void initBots() {
    g_bots.clear();
    Team bt = (g_playerTeam == TEAM_T) ? TEAM_CT : TEAM_T;
    if (g_currentMap == MAP_AZTEC) {
        if (bt == TEAM_CT) {
            g_bots.push_back(makeBot(floorPoint(pxX(215), pxZ(205)), bt, 0));
            g_bots.push_back(makeBot(floorPoint(pxX(520), pxZ(600)), bt, 0));
            g_bots.push_back(makeBot(floorPoint(pxX(660), pxZ(322)), bt, 0));
            g_bots.push_back(makeBot(floorPoint(pxX(460), pxZ(330)), bt, 0));
            g_bots.push_back(makeBot(floorPoint(pxX(950), pxZ(330)), bt, 180));
        } else {
            g_bots.push_back(makeBot(floorPoint(pxX(64), pxZ(500)), bt, 0));
            g_bots.push_back(makeBot(floorPoint(pxX(70), pxZ(300)), bt, 0));
            g_bots.push_back(makeBot(floorPoint(pxX(250), pxZ(560)), bt, 0));
            g_bots.push_back(makeBot(floorPoint(pxX(220), pxZ(390)), bt, 0));
            g_bots.push_back(makeBot(floorPoint(pxX(330), pxZ(405)), bt, 0));
        }
    } else {
        g_bots.push_back(makeBot(floorPoint(0, -40), bt, 90));
        g_bots.push_back(makeBot(floorPoint(-20, -10), bt, 45));
        g_bots.push_back(makeBot(floorPoint(20, -10), bt, -45));
        g_bots.push_back(makeBot(floorPoint(0, 10), bt, 90));
    }
}

// ----------------------------------------------------------------------------
// C4 bomb, rounds
// ----------------------------------------------------------------------------
struct BombState {
    bool planted, exploding;
    glm::vec3 pos;
    float timer, plantProgress, defuseProgress, beepTimer, blink, fx;
};
BombState g_bomb;
void resetBomb() {
    g_bomb.planted = false; g_bomb.exploding = false; g_bomb.pos = glm::vec3(0);
    g_bomb.timer = 40.0f; g_bomb.plantProgress = 0; g_bomb.defuseProgress = 0;
    g_bomb.beepTimer = 1.0f; g_bomb.blink = 0; g_bomb.fx = 0;
}

bool  g_roundOver = false;
float g_roundEndTimer = 0, g_roundTime = 115.0f;

void endRound(Team winner, const std::string& text) {
    if (g_roundOver || g_training) return;
    g_roundOver = true; g_roundEndTimer = 6.0f; g_banner = text;
    if (winner == TEAM_T) g_scoreT++; else g_scoreCT++;
    g_money = std::min(16000, g_money + (winner == g_playerTeam ? 3250 : 1400));
}

void killPlayer() {
    if (g_dead) return;
    g_dead = true; g_deathTimer = 0.0f; g_health = 0;
    g_scoped = false; g_reloadTimer = 0.0f;
    g_bomb.plantProgress = 0; g_bomb.defuseProgress = 0;
    g_deaths++;
    Team enemy = (g_playerTeam == TEAM_T) ? TEAM_CT : TEAM_T;
    if (g_playerTeam == TEAM_T && g_bomb.planted) return;   // bomb will still explode -> T win
    endRound(enemy, "YOU WERE ELIMINATED");
}

void damagePlayer(float dmg) {
    if (g_dead || g_roundOver) return;
    if (g_armor > 0) {                                      // armor absorbs half of the damage
        float absorbed = std::min((float)g_armor, dmg * 0.5f);
        g_armor -= (int)ceil(absorbed);
        dmg -= absorbed;
    }
    g_health -= (int)ceil(dmg);
    g_damageFlash = 0.45f;
    if (g_health <= 0) { g_health = 0; killPlayer(); }
}

void resetRound() {
    bool wasDead = g_dead;
    g_dead = false; g_deathTimer = 0; g_roundOver = false; g_roundTime = 115.0f; g_banner.clear();
    resetBomb();
    if (wasDead || !g_inv[SLOT_KNIFE].has) equipDefaultWeapons();
    if (g_playerTeam == TEAM_T && !g_inv[SLOT_C4].has) giveWeapon(WEAPON_C4);
    if (g_playerTeam == TEAM_CT) g_inv[SLOT_C4].has = false;
    ensureActiveValid();
    g_health = 100; g_armor = 100;
    g_feet = spawnPos(g_playerTeam); g_vel = glm::vec3(0); g_grounded = false;
    currentEyeHeight = EYE_STAND; pitch = 0.0f;
    if (g_currentMap == MAP_AZTEC) yaw = (g_playerTeam == TEAM_T) ? 0.0f : 180.0f; else yaw = -90.0f;
    g_scoped = false; g_reloadTimer = 0; g_shotCooldown = 0.3f; g_damageFlash = 0;
    initBots();
}

// ----------------------------------------------------------------------------
// Rain
// ----------------------------------------------------------------------------
struct RainDrop { glm::vec3 pos; };
std::vector<RainDrop> g_rain;
const int RAIN_COUNT = 260;
float frand(float a, float b) { return a + (b - a) * (float)(rand() % 10000) / 10000.0f; }
void initRain() {
    g_rain.resize(RAIN_COUNT);
    for (auto& r : g_rain) r.pos = glm::vec3(frand(-70, 70), frand(0, 25), frand(-60, 60));
}
void updateRain(float dt) {
    for (auto& r : g_rain) {
        r.pos.y -= 28.0f * dt;
        if (r.pos.y < -1.0f) r.pos = glm::vec3(frand(-70, 70), 22.0f + frand(0, 6), frand(-60, 60));
    }
}

// ----------------------------------------------------------------------------
// OpenGL resources
// ----------------------------------------------------------------------------
GLuint cubeVAO, cubeVBO, uiQuadVAO, uiQuadVBO;

float cubeVertices[] = {
    -0.5f,-0.5f,-0.5f,0,0,-1,  0.5f,-0.5f,-0.5f,0,0,-1,  0.5f,0.5f,-0.5f,0,0,-1,
     0.5f,0.5f,-0.5f,0,0,-1, -0.5f,0.5f,-0.5f,0,0,-1, -0.5f,-0.5f,-0.5f,0,0,-1,
    -0.5f,-0.5f,0.5f,0,0,1,   0.5f,-0.5f,0.5f,0,0,1,   0.5f,0.5f,0.5f,0,0,1,
     0.5f,0.5f,0.5f,0,0,1,  -0.5f,0.5f,0.5f,0,0,1,  -0.5f,-0.5f,0.5f,0,0,1,
    -0.5f,0.5f,0.5f,-1,0,0, -0.5f,0.5f,-0.5f,-1,0,0, -0.5f,-0.5f,-0.5f,-1,0,0,
    -0.5f,-0.5f,-0.5f,-1,0,0,-0.5f,-0.5f,0.5f,-1,0,0, -0.5f,0.5f,0.5f,-1,0,0,
     0.5f,0.5f,0.5f,1,0,0,   0.5f,0.5f,-0.5f,1,0,0,   0.5f,-0.5f,-0.5f,1,0,0,
     0.5f,-0.5f,-0.5f,1,0,0, 0.5f,-0.5f,0.5f,1,0,0,   0.5f,0.5f,0.5f,1,0,0,
    -0.5f,-0.5f,-0.5f,0,-1,0, 0.5f,-0.5f,-0.5f,0,-1,0, 0.5f,-0.5f,0.5f,0,-1,0,
     0.5f,-0.5f,0.5f,0,-1,0, -0.5f,-0.5f,0.5f,0,-1,0, -0.5f,-0.5f,-0.5f,0,-1,0,
    -0.5f,0.5f,-0.5f,0,1,0,  0.5f,0.5f,-0.5f,0,1,0,   0.5f,0.5f,0.5f,0,1,0,
     0.5f,0.5f,0.5f,0,1,0, -0.5f,0.5f,0.5f,0,1,0,   -0.5f,0.5f,-0.5f,0,1,0,
};

void buildMeshes() {
    glGenVertexArrays(1, &cubeVAO); glGenBuffers(1, &cubeVBO);
    glBindVertexArray(cubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    float quad[] = { 0,0, 1,0, 1,1,  0,0, 1,1, 0,1 };
    glGenVertexArrays(1, &uiQuadVAO); glGenBuffers(1, &uiQuadVBO);
    glBindVertexArray(uiQuadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, uiQuadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
}

const char* WORLD_VS = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
uniform mat4 model, view, projection;
uniform mat3 normalMatrix;
out vec3 FragPos; out vec3 Normal;
void main(){
    vec4 world = model * vec4(aPos, 1.0);
    FragPos = world.xyz;
    Normal = normalize(normalMatrix * aNormal);
    gl_Position = projection * view * world;
}
)";

const char* WORLD_FS = R"(
#version 330 core
in vec3 FragPos; in vec3 Normal;
out vec4 FragColor;
uniform vec3 objectColor, viewPos, sunDir, sunColor, fogColor;
uniform float fogStart, fogEnd, uTime;
uniform int isWater;
void main(){
    vec3 N = normalize(Normal);
    vec3 L = normalize(-sunDir);
    vec3 V = normalize(viewPos - FragPos);
    vec3 R = reflect(-L, N);
    float dist = length(viewPos - FragPos);

    vec3 baseCol = objectColor;
    if (isWater == 1) {
        float wave = sin(uTime * 3.0 + FragPos.x * 0.4 + FragPos.z * 0.4) * 0.05;
        baseCol += vec3(wave, wave * 1.2, wave * 1.5);
    } else {
        vec2 uv = abs(N.y) > 0.5 ? FragPos.xz : (abs(N.x) > 0.5 ? FragPos.zy : FragPos.xy);
        vec2 f = fract(uv * 0.5);
        float line = max(step(f.x, 0.04), step(f.y, 0.04));
        line *= clamp(1.0 - dist / 60.0, 0.0, 1.0);
        baseCol *= 1.0 - 0.14 * line;
    }
    float hemi = 0.5 + 0.5 * N.y;
    vec3 ambient = (0.28 + 0.24 * hemi) * sunColor;
    vec3 diffuse = max(dot(N, L), 0.0) * sunColor * 0.8;
    vec3 spec = pow(max(dot(V, R), 0.0), 16.0) * 0.15 * sunColor;
    vec3 lit = (ambient + diffuse + spec) * baseCol;

    float fog = clamp((fogEnd - dist) / (fogEnd - fogStart), 0.0, 1.0);
    FragColor = vec4(mix(fogColor, lit, fog), isWater == 1 ? 0.8 : 1.0);
}
)";

const char* UNLIT_VS = R"(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 model, view, projection;
void main(){ gl_Position = projection * view * model * vec4(aPos, 1.0); }
)";
const char* UNLIT_FS = R"(
#version 330 core
out vec4 FragColor;
uniform vec3 color; uniform float alpha;
void main(){ FragColor = vec4(color, alpha); }
)";
const char* UI_VS = R"(
#version 330 core
layout(location=0) in vec2 aPos;
uniform mat4 projection, model;
void main(){ gl_Position = projection * model * vec4(aPos, 0.0, 1.0); }
)";
const char* UI_FS = UNLIT_FS;

GLuint compileShader(GLenum type, const char* s) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &s, nullptr);
    glCompileShader(sh);
    GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(sh, 1024, nullptr, log); std::cerr << "Shader error:\n" << log << std::endl; }
    return sh;
}
GLuint createProgram(const char* vs, const char* fs) {
    GLuint v = compileShader(GL_VERTEX_SHADER, vs), f = compileShader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(p, 1024, nullptr, log); std::cerr << "Link error:\n" << log << std::endl; }
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

GLuint g_worldProg, g_unlitProg, g_uiProg;
struct WorldLocs { GLint model, view, proj, normalMat, color, viewPos, sunDir, sunColor, fogColor, fogStart, fogEnd, uTime, isWater; } wl;
struct UnlitLocs { GLint model, view, proj, color, alpha; } ul;
struct UILocs { GLint proj, model, color, alpha; } uil;

void cacheLocations() {
    wl.model = glGetUniformLocation(g_worldProg, "model");      wl.view = glGetUniformLocation(g_worldProg, "view");
    wl.proj = glGetUniformLocation(g_worldProg, "projection");  wl.normalMat = glGetUniformLocation(g_worldProg, "normalMatrix");
    wl.color = glGetUniformLocation(g_worldProg, "objectColor"); wl.viewPos = glGetUniformLocation(g_worldProg, "viewPos");
    wl.sunDir = glGetUniformLocation(g_worldProg, "sunDir");    wl.sunColor = glGetUniformLocation(g_worldProg, "sunColor");
    wl.fogColor = glGetUniformLocation(g_worldProg, "fogColor"); wl.fogStart = glGetUniformLocation(g_worldProg, "fogStart");
    wl.fogEnd = glGetUniformLocation(g_worldProg, "fogEnd");    wl.uTime = glGetUniformLocation(g_worldProg, "uTime");
    wl.isWater = glGetUniformLocation(g_worldProg, "isWater");
    ul.model = glGetUniformLocation(g_unlitProg, "model");      ul.view = glGetUniformLocation(g_unlitProg, "view");
    ul.proj = glGetUniformLocation(g_unlitProg, "projection");  ul.color = glGetUniformLocation(g_unlitProg, "color");
    ul.alpha = glGetUniformLocation(g_unlitProg, "alpha");
    uil.proj = glGetUniformLocation(g_uiProg, "projection");    uil.model = glGetUniformLocation(g_uiProg, "model");
    uil.color = glGetUniformLocation(g_uiProg, "color");        uil.alpha = glGetUniformLocation(g_uiProg, "alpha");
}

// ----------------------------------------------------------------------------
// 2D renderer + BITMAP FONT (5x7)  -> this is what makes all text visible
// ----------------------------------------------------------------------------
glm::mat4 g_uiProjection;

void drawRect(float x, float y, float w, float h, glm::vec3 col, float alpha = 1.0f) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f));
    model = glm::scale(model, glm::vec3(w, h, 1.0f));
    glUseProgram(g_uiProg);
    glUniformMatrix4fv(uil.proj, 1, GL_FALSE, glm::value_ptr(g_uiProjection));
    glUniformMatrix4fv(uil.model, 1, GL_FALSE, glm::value_ptr(model));
    glUniform3fv(uil.color, 1, glm::value_ptr(col));
    glUniform1f(uil.alpha, alpha);
    glBindVertexArray(uiQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static const char* FONT_CHARS = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-:./$!+()";
static const uint8_t FONT_DATA[][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // space
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 0
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 1
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, // 2
    {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}, // 3
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 4
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 5
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 6
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 7
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 8
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 9
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // A
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // B
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // C
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, // D
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // E
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // F
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // G
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // H
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // I
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // J
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // K
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // L
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // M
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, // N
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // O
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // P
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // Q
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // R
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // S
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // T
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // U
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // V
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, // W
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // X
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // Y
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // Z
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // -
    {0x00,0x04,0x00,0x00,0x00,0x04,0x00}, // :
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, // .
    {0x01,0x02,0x02,0x04,0x08,0x08,0x10}, // /
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, // $
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, // !
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, // +
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, // (
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}  // )
};

float textW(const std::string& s, float sc) { return s.empty() ? 0.0f : (float)s.size() * 6.0f * sc - sc; }

void drawText(float x, float y, float s, glm::vec3 col, const std::string& str, float alpha = 1.0f, bool shadow = false) {
    if (shadow) drawText(x + s, y + s, s, glm::vec3(0.0f), str, alpha * 0.8f, false);
    float cx = x;
    for (char ch : str) {
        const char* p = ch ? strchr(FONT_CHARS, std::toupper((unsigned char)ch)) : nullptr;
        if (p) {
            const uint8_t* g = FONT_DATA[p - FONT_CHARS];
            for (int r = 0; r < 7; r++) {
                int c = 0;
                while (c < 5) {
                    if (g[r] & (0x10 >> c)) {
                        int st = c;
                        while (c < 5 && (g[r] & (0x10 >> c))) c++;
                        drawRect(cx + st * s, y + r * s, (c - st) * s, s, col, alpha);
                    } else c++;
                }
            }
        }
        cx += 6.0f * s;
    }
}
void drawTextC(float cx, float y, float s, glm::vec3 col, const std::string& str, float alpha = 1.0f, bool shadow = false) {
    drawText(cx - textW(str, s) * 0.5f, y, s, col, str, alpha, shadow);
}
void drawTextR(float rx, float y, float s, glm::vec3 col, const std::string& str, float alpha = 1.0f, bool shadow = false) {
    drawText(rx - textW(str, s), y, s, col, str, alpha, shadow);
}

struct Rect { float x, y, w, h; };
bool pointInRect(double px, double py, const Rect& r) { return px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h; }

void drawButton(const Rect& r, const char* label, glm::vec3 col, bool hover) {
    glm::vec3 c = hover ? glm::min(col * 1.3f, glm::vec3(1.0f)) : col;
    drawRect(r.x - 3, r.y - 3, r.w + 6, r.h + 6, hover ? glm::vec3(1.0f, 0.85f, 0.4f) : glm::vec3(0.12f), 1.0f);
    drawRect(r.x, r.y, r.w, r.h, c);
    drawTextC(r.x + r.w * 0.5f, r.y + (r.h - 21.0f) * 0.5f, 3.0f, glm::vec3(1.0f), label, 1.0f, true);
}

bool worldToScreen(glm::vec3 worldPos, const glm::mat4& view, const glm::mat4& proj, float& outX, float& outY) {
    glm::vec4 clip = proj * view * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.01f) return false;
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < -1.0f || ndc.z > 1.0f) return false;
    outX = (ndc.x * 0.5f + 0.5f) * g_winW;
    outY = (1.0f - (ndc.y * 0.5f + 0.5f)) * g_winH;
    return true;
}

// ----------------------------------------------------------------------------
// Menu layouts (shared between drawing and clicking)
// ----------------------------------------------------------------------------
Rect mainBtn(int i)  { float bw = 380, bh = 60; return { g_winW * 0.5f - bw * 0.5f, g_winH * 0.5f - 60.0f + i * 80.0f, bw, bh }; }
Rect pauseBtn(int i) { float bw = 340, bh = 54; return { g_winW * 0.5f - bw * 0.5f, g_winH * 0.5f - 110.0f + i * 72.0f, bw, bh }; }
Rect teamCard(int i) { float cw = 300, ch = 330; return { i == 0 ? g_winW * 0.5f - cw - 30.0f : g_winW * 0.5f + 30.0f, g_winH * 0.5f - ch * 0.5f + 20.0f, cw, ch }; }

struct BuyItem { const char* name; int price; int weapon; };   // weapon: >=0 type, -1 armor, -2 ammo
const BuyItem BUY_ITEMS[8] = {
    { "GLOCK-18", 200, WEAPON_GLOCK }, { "USP-S", 200, WEAPON_USP }, { "DESERT EAGLE", 700, WEAPON_DEAGLE },
    { "AK-47", 2700, WEAPON_AK47 },    { "M4A1", 2900, WEAPON_M4A1 }, { "AWP", 4750, WEAPON_AWP },
    { "KEVLAR + HELM", 1000, -1 },     { "AMMO REFILL", 100, -2 }
};
Rect buyCard(int i) {
    float cw = 240, ch = 110, gap = 20;
    float sx = g_winW * 0.5f - (3 * cw + 2 * gap) * 0.5f, sy = 170.0f;
    return { sx + (i % 3) * (cw + gap), sy + (i / 3) * (ch + gap), cw, ch };
}
bool itemTeamOk(const BuyItem& it) {
    if (it.weapon < 0) return true;
    Team t = WEAPON_DB[it.weapon].team;
    return t == TEAM_NONE || t == g_playerTeam;
}
bool itemOwned(const BuyItem& it) {
    if (it.weapon < 0) return it.weapon == -1 && g_armor >= 100;
    const WeaponSlot& s = g_inv[WEAPON_DB[it.weapon].slot];
    return s.has && s.type == (WeaponType)it.weapon;
}
void buyItem(const BuyItem& it) {
    if (!itemTeamOk(it)) { showMsg("NOT AVAILABLE FOR YOUR TEAM"); return; }
    if (itemOwned(it))   { showMsg("ALREADY OWNED"); return; }
    if (g_money < it.price) { showMsg("NOT ENOUGH MONEY"); return; }
    g_money -= it.price;
    if (it.weapon == -1) { g_armor = 100; }
    else if (it.weapon == -2) {
        for (int i = 0; i < SLOT_COUNT; i++) if (g_inv[i].has) g_inv[i].reserve = WEAPON_DB[g_inv[i].type].maxReserve;
    } else {
        giveWeapon((WeaponType)it.weapon);
        g_activeSlot = WEAPON_DB[it.weapon].slot;
        g_scoped = false; g_reloadTimer = 0;
    }
    showMsg(std::string("BOUGHT ") + it.name);
}

// ----------------------------------------------------------------------------
// Match start
// ----------------------------------------------------------------------------
void startMatch(GLFWwindow* window, MapID map) {
    g_currentMap = map;
    g_training = (map == MAP_TRAINING);
    if (map == MAP_AZTEC) buildWorldAztec(); else buildWorldTraining();
    g_scoreT = g_scoreCT = g_kills = g_deaths = 0;
    g_money = 10000;
    g_dead = false;
    equipDefaultWeapons();
    resetRound();
    g_state = STATE_PLAYING;
    firstMouse = true;
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

// ----------------------------------------------------------------------------
// Shooting
// ----------------------------------------------------------------------------
void playerFire(bool moving) {
    WeaponSlot& w = g_inv[g_activeSlot];
    const WeaponInfo& info = WEAPON_DB[w.type];
    bool knife = (w.type == WEAPON_KNIFE);
    g_shotCooldown = info.fireRate;
    g_recoilTimer = 0.12f;
    if (!knife) {
        w.clip--;
        if (!info.isSniper) { pitch += 0.45f; yaw += frand(-0.15f, 0.15f); pitch = glm::clamp(pitch, -89.0f, 89.0f); }
    }

    glm::vec3 front = getFront();
    glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0, 1, 0)));
    glm::vec3 upv = glm::cross(right, front);
    float spread = 0.0f;
    if (!knife) {
        if (moving) spread = 0.03f;
        if (!g_grounded) spread = 0.08f;
        if (info.isSniper && !g_scoped) spread = 0.12f;
    }
    glm::vec3 dir = glm::normalize(front + right * frand(-1, 1) * spread + upv * frand(-1, 1) * spread);
    glm::vec3 ro = eyePos();
    float maxT = knife ? 2.6f : 300.0f;

    float bestT = raycastWorld(ro, dir, maxT);
    int hit = -1; bool head = false;
    for (size_t i = 0; i < g_bots.size(); i++) {
        if (!g_bots[i].alive) continue;
        glm::vec3 bmin = g_bots[i].pos + glm::vec3(-0.4f, 0.0f, -0.4f);
        glm::vec3 bmax = g_bots[i].pos + glm::vec3(0.4f, 1.8f, 0.4f);
        float t;
        if (rayAABB(ro, dir, bmin, bmax, t) && t < bestT && t <= maxT) {
            bestT = t; hit = (int)i;
            head = (ro.y + dir.y * t) > g_bots[i].pos.y + 1.45f;
        }
    }
    if (!knife) {
        g_tracerStart = ro + right * 0.2f - upv * 0.15f + dir * 0.6f;
        g_tracerEnd = ro + dir * std::min(bestT, 150.0f);
        g_tracerTimer = 0.05f;
    }
    if (hit >= 0) {
        Bot& b = g_bots[hit];
        b.health -= info.damage * (head ? 2.5f : 1.0f);
        g_hitMarkerTimer = 0.15f;
        if (b.health <= 0.0f) {
            b.alive = false; b.respawnTimer = 4.0f;
            g_kills++;
            g_money = std::min(16000, g_money + (knife ? 1500 : 300));
            showMsg(head ? "HEADSHOT KILL  +$300" : "ENEMY DOWN  +$300");
        }
    }
}

// ----------------------------------------------------------------------------
// Player update
// ----------------------------------------------------------------------------
void stepVertical(float dt, bool jumpPressed) {
    if (g_grounded) {
        float ground = groundAt(g_feet, PLAYER_R * 0.8f, g_feet.y + STEP_H);
        if (jumpPressed && !g_dead) { g_vel.y = JUMP_SPEED; g_grounded = false; }
        else if (g_feet.y - ground > 0.3f) g_grounded = false;          // walked off a ledge
        else { g_feet.y = ground; g_vel.y = 0.0f; }
    }
    if (!g_grounded) {
        float oldY = g_feet.y;
        g_vel.y += GRAVITY * dt;
        g_feet.y += g_vel.y * dt;
        float gr = groundAt(g_feet, PLAYER_R * 0.8f, oldY + STEP_H);
        if (g_vel.y <= 0.0f && g_feet.y <= gr) { g_feet.y = gr; g_vel.y = 0.0f; g_grounded = true; }
    }
    if (g_feet.y < -25.0f) {                                              // safety net: fell out of the world
        g_feet = spawnPos(g_playerTeam); g_vel = glm::vec3(0); g_grounded = false;
    }
}

void updatePlayer(GLFWwindow* window, float dt) {
    static bool spacePrev = false, rPrev = false, lmbPrev = false;
    bool spaceDown = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    bool spacePressed = spaceDown && !spacePrev;
    spacePrev = spaceDown;

    if (g_shotCooldown > 0) g_shotCooldown -= dt;
    if (g_recoilTimer > 0) g_recoilTimer -= dt;
    if (g_hitMarkerTimer > 0) g_hitMarkerTimer -= dt;
    if (g_damageFlash > 0) g_damageFlash -= dt;
    if (g_tracerTimer > 0) g_tracerTimer -= dt;
    if (g_msgTimer > 0) g_msgTimer -= dt;

    // ---------------- DEAD ----------------
    if (g_dead) {
        g_deathTimer += dt;
        currentEyeHeight += (0.3f - currentEyeHeight) * std::min(1.0f, dt * 5.0f);
        stepVertical(dt, false);
        bool canRespawn = g_training ? (g_deathTimer > 1.5f) : (g_roundOver && g_roundEndTimer < 4.0f);
        if (canRespawn && (spacePressed || (g_training && g_deathTimer > 4.0f))) resetRound();
        lmbPrev = true;
        return;
    }

    // ---------------- ALIVE ----------------
    isCrouching = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
    float targetEye = isCrouching ? EYE_CROUCH : EYE_STAND;
    currentEyeHeight += (targetEye - currentEyeHeight) * std::min(1.0f, dt * 10.0f);

    glm::vec3 front = getFront();
    glm::vec3 flat = glm::normalize(glm::vec3(front.x, 0.0f, front.z));
    glm::vec3 right = glm::normalize(glm::cross(flat, glm::vec3(0, 1, 0)));
    glm::vec3 wish(0.0f);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) wish += flat;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) wish -= flat;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) wish += right;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) wish -= right;
    bool moving = glm::length(wish) > 0.01f;
    if (moving) wish = glm::normalize(wish);
    float speed = isCrouching ? 3.5f : (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? 11.0f : 7.0f);
    if (g_scoped) speed *= 0.5f;
    g_feet.x += wish.x * speed * dt;
    g_feet.z += wish.z * speed * dt;
    resolveXZ(g_feet, PLAYER_R, PLAYER_H);
    stepVertical(dt, spacePressed);

    // ---------------- weapons ----------------
    ensureActiveValid();
    WeaponSlot& cur = g_inv[g_activeSlot];
    const WeaponInfo& info = WEAPON_DB[cur.type];

    bool rDown = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
    if (rDown && !rPrev && g_reloadTimer <= 0.0f && info.maxClip > 0 && cur.clip < info.maxClip && cur.reserve > 0) {
        g_reloadTimer = info.reloadTime; g_scoped = false;
    }
    rPrev = rDown;
    if (g_reloadTimer > 0.0f) {
        g_reloadTimer -= dt;
        if (g_reloadTimer <= 0.0f) {
            int take = std::min(info.maxClip - cur.clip, cur.reserve);
            cur.clip += take; cur.reserve -= take;
        }
    }

    bool lmbDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    bool wantFire = info.isAutomatic ? lmbDown : (lmbDown && !lmbPrev);
    lmbPrev = lmbDown;
    if (cur.type != WEAPON_C4 && wantFire && g_shotCooldown <= 0.0f && g_reloadTimer <= 0.0f) {
        if (cur.type == WEAPON_KNIFE || cur.clip > 0) playerFire(moving);
        else if (cur.reserve > 0) { g_reloadTimer = info.reloadTime; g_scoped = false; }
    }

    // ---------------- C4 plant ----------------
    bool eDown = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
    if (g_playerTeam == TEAM_T && cur.type == WEAPON_C4 && !g_bomb.planted && !g_roundOver) {
        bool inSite = false;
        for (auto& s : g_bombSites)
            if (g_feet.x >= s.minAABB.x && g_feet.x <= s.maxAABB.x && g_feet.z >= s.minAABB.z && g_feet.z <= s.maxAABB.z) inSite = true;
        if (inSite) g_hint = "HOLD E - PLANT THE BOMB"; else g_hint = "GO TO A BOMBSITE (A / B)";
        if (inSite && eDown && g_grounded) {
            g_bomb.plantProgress += dt;
            if (g_bomb.plantProgress >= 3.2f) {
                g_bomb.planted = true; g_bomb.pos = g_feet + glm::vec3(0, 0.15f, 0);
                g_bomb.timer = 40.0f; g_bomb.plantProgress = 0.0f; g_bomb.beepTimer = 0.0f;
                g_inv[SLOT_C4].has = false; ensureActiveValid();
                g_money = std::min(16000, g_money + 300);
                showMsg("THE BOMB HAS BEEN PLANTED");
            }
        } else g_bomb.plantProgress = 0.0f;
    } else g_bomb.plantProgress = 0.0f;

    // ---------------- C4 defuse ----------------
    if (g_playerTeam == TEAM_CT && g_bomb.planted && !g_bomb.exploding && !g_roundOver) {
        glm::vec3 d = g_feet - g_bomb.pos; d.y = 0;
        if (glm::length(d) < 2.5f) {
            g_hint = "HOLD E - DEFUSE THE BOMB";
            if (eDown) {
                g_bomb.defuseProgress += dt;
                if (g_bomb.defuseProgress >= 5.0f) {
                    g_bomb.planted = false; g_bomb.defuseProgress = 0;
                    g_money = std::min(16000, g_money + 300);
                    endRound(TEAM_CT, "BOMB DEFUSED - COUNTER-TERRORISTS WIN");
                }
            } else g_bomb.defuseProgress = 0.0f;
        } else g_bomb.defuseProgress = 0.0f;
    } else g_bomb.defuseProgress = 0.0f;
}

void updateBomb(float dt) {
    if (g_bomb.exploding) { g_bomb.fx += dt; return; }
    if (!g_bomb.planted) return;
    g_bomb.timer -= dt;
    g_bomb.beepTimer -= dt;
    if (g_bomb.blink > 0) g_bomb.blink -= dt;
    if (g_bomb.beepTimer <= 0.0f) { g_bomb.beepTimer = glm::clamp(g_bomb.timer / 40.0f, 0.1f, 1.0f); g_bomb.blink = 0.12f; }
    if (g_bomb.timer <= 0.0f) {
        g_bomb.exploding = true; g_bomb.planted = false; g_bomb.fx = 0.0f;
        if (!g_dead && glm::distance(g_feet, g_bomb.pos) < 35.0f) { g_health = 0; killPlayer(); }
        endRound(TEAM_T, "BOMB EXPLODED - TERRORISTS WIN");
    }
}

void updateBots(float dt) {
    glm::vec3 eye = eyePos();
    for (auto& b : g_bots) {
        if (b.flash > 0) b.flash -= dt;
        if (!b.alive) {
            if (g_training) { b.respawnTimer -= dt; if (b.respawnTimer <= 0) { b.alive = true; b.health = 100; b.pos = b.home; } }
            continue;
        }
        glm::vec3 botEye = b.pos + glm::vec3(0, 1.6f, 0);
        glm::vec3 to = eye - botEye;
        float dist = glm::length(to);
        bool sees = false;
        if (!g_dead && !g_roundOver && dist < 60.0f && dist > 0.01f) sees = raycastWorld(botEye, to / dist, dist) >= dist;

        glm::vec3 move(0.0f); float speed = 0.0f;
        if (sees) {
            float target = glm::degrees(atan2(to.z, to.x));
            float diff = target - b.yaw;
            while (diff > 180.0f) diff -= 360.0f;
            while (diff < -180.0f) diff += 360.0f;
            b.yaw += diff * std::min(1.0f, dt * 8.0f);
            b.wanderTimer -= dt;
            if (b.wanderTimer <= 0.0f) { b.wanderTimer = frand(0.6f, 1.6f); b.strafe = (rand() % 2) ? 1.0f : -1.0f; }
            glm::vec3 fwd = to / dist; fwd.y = 0.0f;
            if (glm::length(fwd) > 0.001f) fwd = glm::normalize(fwd);
            glm::vec3 side(-fwd.z, 0.0f, fwd.x);
            if (dist > 14.0f)     { move = fwd * 0.7f + side * b.strafe * 0.5f; speed = 3.5f; }
            else if (dist < 7.0f) { move = -fwd * 0.5f + side * b.strafe;       speed = 3.0f; }
            else                  { move = side * b.strafe;                      speed = 2.5f; }

            b.reactTimer -= dt;
            if (b.reactTimer <= 0.0f) {
                b.shootTimer -= dt;
                if (b.shootTimer <= 0.0f) {
                    b.shootTimer = frand(0.22f, 0.6f);
                    b.flash = 0.06f;
                    float chance = glm::clamp(0.55f - dist / 90.0f, 0.08f, 0.5f);
                    if (isCrouching) chance *= 0.8f;
                    if (frand(0, 1) < chance) damagePlayer(9.0f + (float)(rand() % 8));
                }
            }
        } else {
            b.reactTimer = 0.45f;
            b.wanderTimer -= dt;
            if (b.wanderTimer <= 0.0f) {
                b.wanderTimer = frand(2.0f, 5.0f);
                glm::vec3 tgt = b.home + glm::vec3(frand(-7, 7), 0, frand(-7, 7));
                glm::vec2 d(tgt.x - b.pos.x, tgt.z - b.pos.z);
                b.wanderDir = glm::length(d) > 0.5f ? glm::normalize(d) : glm::vec2(0);
            }
            if (glm::length(b.wanderDir) > 0.01f) {
                move = glm::vec3(b.wanderDir.x, 0, b.wanderDir.y); speed = 1.8f;
                b.yaw = glm::degrees(atan2(move.z, move.x));
            }
        }
        b.moving = speed > 0.0f;
        if (b.moving) {
            glm::vec3 dir = glm::normalize(move);
            b.pos.x += dir.x * speed * dt; b.pos.z += dir.z * speed * dt;
            resolveXZ(b.pos, 0.45f, 1.8f);
            b.walkPhase += dt * speed * 3.0f;
        }
        // do not stand inside the player
        float dx = b.pos.x - g_feet.x, dz = b.pos.z - g_feet.z, d2 = dx * dx + dz * dz;
        if (d2 < 0.64f && d2 > 1e-6f) { float d = sqrt(d2); b.pos.x += dx / d * (0.8f - d); b.pos.z += dz / d * (0.8f - d); }
        float g = groundAt(b.pos, 0.35f, b.pos.y + STEP_H);
        if (g > -500.0f) b.pos.y = g;
    }
}

void updateRound(float dt) {
    if (!g_training && !g_roundOver) {
        if (!g_bomb.planted && !g_bomb.exploding) {
            g_roundTime -= dt;
            if (g_roundTime <= 0.0f) { g_roundTime = 0; endRound(TEAM_CT, "TIME IS UP - COUNTER-TERRORISTS WIN"); }
        }
        int alive = 0;
        for (auto& b : g_bots) if (b.alive) alive++;
        if (alive == 0) {
            if (g_playerTeam == TEAM_T) endRound(TEAM_T, "ENEMIES ELIMINATED - TERRORISTS WIN");
            else if (!g_bomb.planted) endRound(TEAM_CT, "ENEMIES ELIMINATED - COUNTER-TERRORISTS WIN");
        }
    }
    if (g_roundOver) {
        g_roundEndTimer -= dt;
        if (g_roundEndTimer <= 0.0f) resetRound();
    }
}

void updateGame(GLFWwindow* window, float dt) {
    g_hint.clear();
    updatePlayer(window, dt);
    updateBomb(dt);
    updateBots(dt);
    updateRound(dt);
    if (g_currentMap == MAP_AZTEC) updateRain(dt);
}

// ----------------------------------------------------------------------------
// 3D rendering
// ----------------------------------------------------------------------------
void drawWorldBox(const glm::mat4& model, const glm::mat3& nm, const glm::vec3& col) {
    glUniformMatrix4fv(wl.model, 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix3fv(wl.normalMat, 1, GL_FALSE, glm::value_ptr(nm));
    glUniform3fv(wl.color, 1, glm::value_ptr(col));
    glDrawArrays(GL_TRIANGLES, 0, 36);
}
void drawPart(const glm::mat4& base, glm::vec3 off, glm::vec3 size, glm::vec3 col) {
    glm::mat4 m = glm::translate(base, off);
    m = glm::scale(m, size);
    drawWorldBox(m, glm::mat3(base), col);
}
void drawUnlitBox(const glm::mat4& m, glm::vec3 col, float alpha) {
    glUniformMatrix4fv(ul.model, 1, GL_FALSE, glm::value_ptr(m));
    glUniform3fv(ul.color, 1, glm::value_ptr(col));
    glUniform1f(ul.alpha, alpha);
    glDrawArrays(GL_TRIANGLES, 0, 36);
}

void drawBot(const Bot& b) {
    glm::vec3 body = (b.team == TEAM_T) ? glm::vec3(0.62f, 0.47f, 0.20f) : glm::vec3(0.20f, 0.32f, 0.62f);
    glm::vec3 legs = body * 0.55f, skin(0.85f, 0.68f, 0.52f), gun(0.12f, 0.12f, 0.13f);
    glm::mat4 base = glm::translate(glm::mat4(1.0f), b.pos);
    base = glm::rotate(base, -glm::radians(b.yaw), glm::vec3(0, 1, 0));
    if (!b.alive) {
        base = glm::translate(glm::mat4(1.0f), b.pos + glm::vec3(0, 0.25f, 0));
        base = glm::rotate(base, -glm::radians(b.yaw), glm::vec3(0, 1, 0));
        base = glm::rotate(base, glm::radians(90.0f), glm::vec3(0, 0, 1));
    }
    float sw = (b.alive && b.moving) ? sin(b.walkPhase) * 0.22f : 0.0f;
    drawPart(base, glm::vec3(sw, 0.4f, 0.17f), glm::vec3(0.26f, 0.8f, 0.26f), legs);
    drawPart(base, glm::vec3(-sw, 0.4f, -0.17f), glm::vec3(0.26f, 0.8f, 0.26f), legs);
    drawPart(base, glm::vec3(0, 1.15f, 0), glm::vec3(0.36f, 0.7f, 0.62f), body);
    drawPart(base, glm::vec3(0, 1.62f, 0), glm::vec3(0.32f, 0.32f, 0.32f), skin);
    drawPart(base, glm::vec3(0, 1.8f, 0), glm::vec3(0.36f, 0.1f, 0.36f), body * 0.7f);
    drawPart(base, glm::vec3(0.3f, 1.25f, 0.34f), glm::vec3(0.5f, 0.14f, 0.14f), body * 0.85f);
    drawPart(base, glm::vec3(0.3f, 1.25f, -0.34f), glm::vec3(0.5f, 0.14f, 0.14f), body * 0.85f);
    drawPart(base, glm::vec3(0.55f, 1.2f, 0.0f), glm::vec3(0.75f, 0.12f, 0.1f), gun);
}

void renderWorld(float now) {
    float aspect = (float)g_fbW / (float)std::max(1, g_fbH);
    float fov = g_scoped ? 22.0f : 75.0f;
    glm::vec3 eye = eyePos();
    glm::mat4 projection = glm::perspective(glm::radians(fov), aspect, 0.1f, 400.0f);
    glm::mat4 view = glm::lookAt(eye, eye + getFront(), glm::vec3(0, 1, 0));
    const float FOG_END = 150.0f;

    glEnable(GL_DEPTH_TEST);
    glUseProgram(g_worldProg);
    glUniformMatrix4fv(wl.proj, 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(wl.view, 1, GL_FALSE, glm::value_ptr(view));
    glUniform3fv(wl.viewPos, 1, glm::value_ptr(eye));
    glUniform3f(wl.sunDir, -0.3f, -1.0f, -0.4f);
    glUniform3f(wl.sunColor, 0.95f, 0.93f, 0.88f);
    glUniform3f(wl.fogColor, 0.52f, 0.55f, 0.58f);
    glUniform1f(wl.fogStart, 30.0f);
    glUniform1f(wl.fogEnd, FOG_END);
    glUniform1f(wl.uTime, now);
    glBindVertexArray(cubeVAO);

    // opaque world
    glUniform1i(wl.isWater, 0);
    glm::mat3 I(1.0f);
    for (const auto& b : g_world) {
        if (b.isWater) continue;
        if (glm::length(b.center - eye) - glm::length(b.halfSize) > FOG_END + 10.0f) continue;
        glm::mat4 m = glm::translate(glm::mat4(1.0f), b.center);
        m = glm::scale(m, b.halfSize * 2.0f);
        drawWorldBox(m, I, b.color);
    }
    for (const auto& bot : g_bots) drawBot(bot);
    if (g_bomb.planted) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), g_bomb.pos + glm::vec3(0, 0.15f, 0));
        m = glm::scale(m, glm::vec3(0.5f, 0.3f, 0.35f));
        drawWorldBox(m, I, glm::vec3(0.15f, 0.15f, 0.15f));
    }

    // transparent water
    glDepthMask(GL_FALSE);
    glUniform1i(wl.isWater, 1);
    for (const auto& b : g_world) {
        if (!b.isWater) continue;
        glm::mat4 m = glm::translate(glm::mat4(1.0f), b.center);
        m = glm::scale(m, b.halfSize * 2.0f);
        drawWorldBox(m, I, b.color);
    }
    glDepthMask(GL_TRUE);
    glUniform1i(wl.isWater, 0);

    // unlit effects: rain, tracer, bomb light, explosion
    glUseProgram(g_unlitProg);
    glUniformMatrix4fv(ul.proj, 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(ul.view, 1, GL_FALSE, glm::value_ptr(view));
    glBindVertexArray(cubeVAO);
    if (g_currentMap == MAP_AZTEC) {
        for (auto& r : g_rain) {
            glm::mat4 m = glm::translate(glm::mat4(1.0f), r.pos);
            m = glm::scale(m, glm::vec3(0.03f, 0.65f, 0.03f));
            drawUnlitBox(m, glm::vec3(0.7f, 0.8f, 0.85f), 0.3f);
        }
    }
    if (g_tracerTimer > 0.0f) {
        float len = glm::length(g_tracerEnd - g_tracerStart);
        if (len > 0.1f) {
            glm::mat4 m = glm::inverse(glm::lookAt(g_tracerStart, g_tracerEnd, glm::vec3(0, 1, 0)));
            m = glm::translate(m, glm::vec3(0, 0, -len * 0.5f));
            m = glm::scale(m, glm::vec3(0.025f, 0.025f, len));
            drawUnlitBox(m, glm::vec3(1.0f, 0.9f, 0.4f), 0.9f);
        }
    }
    if (g_bomb.planted && g_bomb.blink > 0.0f) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), g_bomb.pos + glm::vec3(0, 0.35f, 0));
        m = glm::scale(m, glm::vec3(0.12f));
        drawUnlitBox(m, glm::vec3(1.0f, 0.1f, 0.1f), 1.0f);
    }
    for (const auto& bot : g_bots) {      // muzzle flashes of bots
        if (bot.alive && bot.flash > 0.0f) {
            glm::vec3 p = bot.pos + glm::vec3(cos(glm::radians(bot.yaw)) * 1.0f, 1.2f, sin(glm::radians(bot.yaw)) * 1.0f);
            glm::mat4 m = glm::translate(glm::mat4(1.0f), p);
            m = glm::scale(m, glm::vec3(0.25f));
            drawUnlitBox(m, glm::vec3(1.0f, 0.85f, 0.3f), 0.9f);
        }
    }
    if (g_bomb.exploding && g_bomb.fx < 1.5f) {
        float s = 2.0f + g_bomb.fx * 40.0f;
        glm::mat4 m = glm::translate(glm::mat4(1.0f), g_bomb.pos + glm::vec3(0, 2.0f, 0));
        m = glm::scale(m, glm::vec3(s));
        drawUnlitBox(m, glm::vec3(1.0f, 0.5f, 0.1f), 0.55f * (1.0f - g_bomb.fx / 1.5f));
    }

    // first-person weapon
    if (!g_scoped && !g_dead) {
        glClear(GL_DEPTH_BUFFER_BIT);
        glm::mat4 gunProj = glm::perspective(glm::radians(70.0f), aspect, 0.01f, 10.0f);
        glUniformMatrix4fv(ul.proj, 1, GL_FALSE, glm::value_ptr(gunProj));
        glUniformMatrix4fv(ul.view, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
        float kick = (g_recoilTimer > 0.0f) ? (g_recoilTimer / 0.12f) * 0.08f : 0.0f;
        float reloadDip = (g_reloadTimer > 0.0f) ? 0.12f : 0.0f;
        glm::vec3 off(0.25f, -0.22f - reloadDip, -0.48f + kick);
        auto part = [&](glm::vec3 rel, glm::vec3 size, glm::vec3 col) {
            glm::mat4 m = glm::translate(glm::mat4(1.0f), off + rel);
            m = glm::scale(m, size);
            drawUnlitBox(m, col, 1.0f);
        };
        WeaponType t = g_inv[g_activeSlot].type;
        if (t == WEAPON_AK47 || t == WEAPON_M4A1) {
            glm::vec3 main = (t == WEAPON_AK47) ? glm::vec3(0.30f, 0.20f, 0.12f) : glm::vec3(0.16f, 0.16f, 0.17f);
            part({ 0, 0, 0 }, { 0.06f, 0.09f, 0.5f }, main);
            part({ 0, 0.02f, -0.35f }, { 0.025f, 0.025f, 0.25f }, { 0.1f, 0.1f, 0.1f });
            part({ 0, -0.1f, 0.1f }, { 0.05f, 0.14f, 0.06f }, { 0.4f, 0.25f, 0.12f });
            part({ 0, -0.09f, -0.05f }, { 0.04f, 0.12f, 0.07f }, { 0.12f, 0.12f, 0.12f });
        } else if (t == WEAPON_AWP) {
            part({ 0, 0, -0.1f }, { 0.07f, 0.08f, 0.7f }, { 0.22f, 0.35f, 0.22f });
            part({ 0, 0.07f, -0.1f }, { 0.05f, 0.05f, 0.3f }, { 0.08f, 0.08f, 0.08f });
        } else if (t == WEAPON_C4) {
            part({ 0, -0.05f, 0.1f }, { 0.16f, 0.1f, 0.22f }, { 0.65f, 0.5f, 0.2f });
            part({ 0, 0.01f, 0.1f }, { 0.12f, 0.03f, 0.1f }, { 0.1f, 0.1f, 0.1f });
        } else if (t == WEAPON_KNIFE) {
            part({ -0.05f, 0.02f, -0.12f }, { 0.015f, 0.08f, 0.35f }, { 0.75f, 0.78f, 0.8f });
            part({ -0.05f, -0.02f, 0.12f }, { 0.04f, 0.04f, 0.14f }, { 0.15f, 0.1f, 0.08f });
        } else {
            part({ 0, 0, 0 }, { 0.05f, 0.07f, 0.25f }, { 0.2f, 0.2f, 0.22f });
            part({ 0, -0.08f, 0.08f }, { 0.04f, 0.12f, 0.06f }, { 0.12f, 0.12f, 0.12f });
        }
        if (g_recoilTimer > 0.07f && t != WEAPON_KNIFE && t != WEAPON_C4)
            part({ 0, 0.01f, -0.55f }, { 0.08f, 0.08f, 0.08f }, { 1.0f, 0.85f, 0.3f });
    }
}

// ----------------------------------------------------------------------------
// HUD
// ----------------------------------------------------------------------------
void drawMinimap() {
    if (g_miniRuns.empty()) return;
    const float sc = 0.22f;
    float ox = g_winW - 1024.0f * sc - 14.0f, oy = 14.0f;
    auto SX = [&](float px) { return ox + px * sc; };
    auto SY = [&](float py) { return oy + (py - 90.0f) * sc; };
    drawRect(ox - 4, oy - 4, 1024.0f * sc + 8, 570.0f * sc + 8, glm::vec3(0.04f), 0.65f);
    for (const auto& r : g_miniRuns) drawRect(SX(r.x), SY(r.y), r.z * sc, CELL * sc + 0.6f, glm::vec3(0.55f, 0.58f, 0.5f), 0.8f);
    for (const auto& s : g_bombSites) {
        float px = worldToPxX(s.markerPos.x), py = worldToPxZ(s.markerPos.z);
        drawTextC(SX(px), SY(py) - 6.0f, 2.0f, glm::vec3(1.0f, 0.3f, 0.3f), std::string(1, s.letter), 1.0f, true);
    }
    if (g_bomb.planted) {
        float px = worldToPxX(g_bomb.pos.x), py = worldToPxZ(g_bomb.pos.z);
        if (g_bomb.blink > 0.0f || fmod(g_bomb.timer, 1.0f) < 0.5f) drawRect(SX(px) - 3, SY(py) - 3, 6, 6, glm::vec3(1.0f, 0.1f, 0.1f));
    }
    float ppx = worldToPxX(g_feet.x), ppy = worldToPxZ(g_feet.z);
    float dx = cos(glm::radians(yaw)), dz = sin(glm::radians(yaw));
    drawRect(SX(ppx) + dx * 7 - 1.5f, SY(ppy) + dz * 7 - 1.5f, 3, 3, glm::vec3(1.0f, 1.0f, 0.3f));
    drawRect(SX(ppx) - 3, SY(ppy) - 3, 6, 6, g_dead ? glm::vec3(0.6f) : glm::vec3(0.2f, 1.0f, 0.3f));
}

void drawHUD() {
    float W = (float)g_winW, H = (float)g_winH;
    glm::vec3 white(1), green(0.2f, 0.9f, 0.3f), blue(0.35f, 0.65f, 0.98f), yellow(0.97f, 0.85f, 0.2f);

    // bombsite letters in the world
    {
        float aspect = (float)g_fbW / (float)std::max(1, g_fbH);
        float fov = g_scoped ? 22.0f : 75.0f;
        glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, 0.1f, 400.0f);
        glm::vec3 e = eyePos();
        glm::mat4 v = glm::lookAt(e, e + getFront(), glm::vec3(0, 1, 0));
        for (auto& s : g_bombSites) {
            float sx, sy;
            if (worldToScreen(s.markerPos + glm::vec3(0, 2.5f, 0), v, proj, sx, sy)) {
                drawTextC(sx, sy - 14.0f, 5.0f, glm::vec3(0.95f, 0.25f, 0.25f), std::string(1, s.letter), 1.0f, true);
                std::string dm = std::to_string((int)glm::distance(e, s.markerPos)) + " M";
                drawTextC(sx, sy + 28.0f, 2.0f, white, dm, 0.85f, true);
            }
        }
    }

    // crosshair / scope
    if (!g_dead) {
        if (g_scoped) {
            drawRect(0, 0, W, H, glm::vec3(0), 0.65f);
            drawRect(0, H * 0.5f - 1, W, 2, glm::vec3(0));
            drawRect(W * 0.5f - 1, 0, 2, H, glm::vec3(0));
        } else {
            glm::vec3 ch = (g_hitMarkerTimer > 0.0f) ? glm::vec3(1, 0.2f, 0.2f) : glm::vec3(0.1f, 0.95f, 0.1f);
            drawRect(W * 0.5f - 9, H * 0.5f - 1, 6, 2, ch); drawRect(W * 0.5f + 3, H * 0.5f - 1, 6, 2, ch);
            drawRect(W * 0.5f - 1, H * 0.5f - 9, 2, 6, ch); drawRect(W * 0.5f - 1, H * 0.5f + 3, 2, 6, ch);
        }
    }
    if (g_damageFlash > 0.0f) drawRect(0, 0, W, H, glm::vec3(0.8f, 0.0f, 0.0f), std::min(0.5f, g_damageFlash));
    if (g_health > 0 && g_health < 30) drawRect(0, 0, W, 6, glm::vec3(0.9f, 0.1f, 0.1f), 0.6f);

    // bottom-left: health / armor
    drawRect(20, H - 84, 320, 64, glm::vec3(0.06f, 0.07f, 0.09f), 0.7f);
    glm::vec3 hpCol = g_health > 30 ? green : glm::vec3(1.0f, 0.25f, 0.2f);
    drawText(32, H - 78, 2.0f, glm::vec3(0.7f), "HEALTH");
    drawText(32, H - 58, 5.0f, hpCol, std::to_string(g_health), 1.0f, true);
    drawText(182, H - 78, 2.0f, glm::vec3(0.7f), "ARMOR");
    drawText(182, H - 58, 5.0f, blue, std::to_string(g_armor), 1.0f, true);

    // bottom-right: weapon + ammo
    {
        const WeaponSlot& cur = g_inv[g_activeSlot];
        const WeaponInfo& wi = WEAPON_DB[cur.type];
        drawRect(W - 340, H - 84, 320, 64, glm::vec3(0.06f, 0.07f, 0.09f), 0.7f);
        drawText(W - 328, H - 78, 2.0f, glm::vec3(0.7f), wi.name);
        if (wi.maxClip > 0) {
            drawText(W - 328, H - 58, 5.0f, yellow, std::to_string(cur.clip), 1.0f, true);
            drawText(W - 328 + textW(std::to_string(cur.clip), 5.0f) + 12, H - 50, 3.0f, glm::vec3(0.75f), "/ " + std::to_string(cur.reserve));
        }
        // slots
        for (int i = 0; i < SLOT_COUNT; i++) {
            if (!g_inv[i].has) continue;
            glm::vec3 c = (i == g_activeSlot) ? yellow : glm::vec3(0.6f);
            drawText(W - 340 + i * 80.0f, H - 108, 2.0f, c, std::to_string(i + 1) + " " + WEAPON_DB[g_inv[i].type].name, (i == g_activeSlot) ? 1.0f : 0.7f, true);
        }
    }

    // top-left: money + keys
    drawText(20, 18, 4.0f, green, "$" + std::to_string(g_money), 1.0f, true);
    drawText(20, 56, 2.0f, glm::vec3(0.85f), "B - BUY MENU    K " + std::to_string(g_kills) + "  D " + std::to_string(g_deaths), 0.9f, true);

    // top-center: round timer + score
    if (!g_training) {
        int t = (int)ceil(g_roundTime);
        std::string ts = std::to_string(t / 60) + ":" + (t % 60 < 10 ? "0" : "") + std::to_string(t % 60);
        drawRect(W * 0.5f - 110, 10, 220, 62, glm::vec3(0.06f, 0.07f, 0.09f), 0.7f);
        drawTextC(W * 0.5f, 16, 4.0f, white, ts, 1.0f, true);
        drawTextC(W * 0.5f - 60, 50, 2.0f, glm::vec3(0.95f, 0.7f, 0.25f), "T " + std::to_string(g_scoreT), 1.0f, true);
        drawTextC(W * 0.5f + 60, 50, 2.0f, blue, "CT " + std::to_string(g_scoreCT), 1.0f, true);
    } else {
        drawTextC(W * 0.5f, 16, 3.0f, white, "TRAINING", 1.0f, true);
    }
    if (g_bomb.planted) {
        drawRect(W * 0.5f - 90, 80, 180, 36, glm::vec3(0.8f, 0.1f, 0.1f), 0.9f);
        drawTextC(W * 0.5f, 88, 3.0f, white, "BOMB " + std::to_string((int)ceil(g_bomb.timer)), 1.0f, true);
    }

    // progress bars
    if (g_reloadTimer > 0.0f) {
        float pct = 1.0f - g_reloadTimer / WEAPON_DB[g_inv[g_activeSlot].type].reloadTime;
        drawRect(W * 0.5f - 80, H - 60, 160, 8, glm::vec3(0.2f));
        drawRect(W * 0.5f - 80, H - 60, 160 * glm::clamp(pct, 0.0f, 1.0f), 8, glm::vec3(0.9f, 0.7f, 0.1f));
        drawTextC(W * 0.5f, H - 80, 2.0f, white, "RELOADING", 1.0f, true);
    }
    if (g_bomb.plantProgress > 0.0f) {
        drawRect(W * 0.5f - 100, H * 0.5f + 60, 200, 12, glm::vec3(0.2f));
        drawRect(W * 0.5f - 100, H * 0.5f + 60, 200 * (g_bomb.plantProgress / 3.2f), 12, glm::vec3(0.9f, 0.3f, 0.2f));
    }
    if (g_bomb.defuseProgress > 0.0f) {
        drawRect(W * 0.5f - 100, H * 0.5f + 60, 200, 12, glm::vec3(0.2f));
        drawRect(W * 0.5f - 100, H * 0.5f + 60, 200 * (g_bomb.defuseProgress / 5.0f), 12, glm::vec3(0.2f, 0.6f, 0.9f));
    }
    if (!g_hint.empty() && !g_dead) drawTextC(W * 0.5f, H * 0.5f + 90, 2.5f, glm::vec3(1.0f, 0.95f, 0.5f), g_hint, 1.0f, true);

    // messages
    if (g_msgTimer > 0.0f) drawTextC(W * 0.5f, 130, 2.5f, white, g_msg, std::min(1.0f, g_msgTimer), true);
    if (g_roundOver && !g_banner.empty()) {
        drawRect(0, H * 0.28f - 12, W, 60, glm::vec3(0), 0.55f);
        drawTextC(W * 0.5f, H * 0.28f, 4.0f, glm::vec3(1.0f, 0.9f, 0.4f), g_banner, 1.0f, true);
    }

    drawMinimap();

    // death screen
    if (g_dead) {
        drawRect(0, 0, W, H, glm::vec3(0.35f, 0.0f, 0.0f), 0.40f);
        drawTextC(W * 0.5f, H * 0.38f, 9.0f, glm::vec3(1.0f, 0.2f, 0.2f), "YOU DIED", 1.0f, true);
        bool can = g_training ? (g_deathTimer > 1.5f) : (g_roundOver && g_roundEndTimer < 4.0f);
        if (can) drawTextC(W * 0.5f, H * 0.38f + 100, 3.0f, white, "PRESS SPACE TO RESPAWN", 1.0f, true);
        else if (!g_training) drawTextC(W * 0.5f, H * 0.38f + 100, 3.0f, glm::vec3(0.85f), "WAITING FOR THE ROUND TO END...", 1.0f, true);
    }
}

// ----------------------------------------------------------------------------
// Menus
// ----------------------------------------------------------------------------
void drawMainMenu() {
    float W = (float)g_winW, H = (float)g_winH;
    drawRect(0, 0, W, H, glm::vec3(0.08f, 0.09f, 0.11f));
    drawRect(0, 0, W, 130, glm::vec3(0.85f, 0.50f, 0.12f));
    drawTextC(W * 0.5f, 28, 8.0f, glm::vec3(1), "COUNTER-STRIKE", 1.0f, true);
    drawTextC(W * 0.5f, 100, 3.0f, glm::vec3(0.15f), "DE_AZTEC EDITION");
    const char* labels[3] = { "PLAY DE_AZTEC", "TRAINING", "QUIT" };
    glm::vec3 cols[3] = { glm::vec3(0.55f, 0.45f, 0.28f), glm::vec3(0.25f, 0.50f, 0.30f), glm::vec3(0.60f, 0.18f, 0.18f) };
    for (int i = 0; i < 3; i++) drawButton(mainBtn(i), labels[i], cols[i], pointInRect(g_mouseX, g_mouseY, mainBtn(i)));
    drawTextC(W * 0.5f, H - 90, 2.0f, glm::vec3(0.7f), "WASD MOVE  SPACE JUMP  CTRL CROUCH  SHIFT SPRINT  R RELOAD");
    drawTextC(W * 0.5f, H - 62, 2.0f, glm::vec3(0.7f), "B BUY MENU  E PLANT / DEFUSE  1-4 WEAPONS  RMB SCOPE (AWP)  ESC PAUSE");
}

void drawTeamSelect() {
    float W = (float)g_winW, H = (float)g_winH;
    drawRect(0, 0, W, H, glm::vec3(0.08f, 0.09f, 0.11f));
    drawTextC(W * 0.5f, 60, 6.0f, glm::vec3(1), "CHOOSE YOUR TEAM", 1.0f, true);
    drawTextC(W * 0.5f, 120, 2.5f, glm::vec3(0.7f), "ESC - BACK");
    Rect t = teamCard(0), ct = teamCard(1);
    bool ht = pointInRect(g_mouseX, g_mouseY, t), hct = pointInRect(g_mouseX, g_mouseY, ct);
    drawRect(t.x - 4, t.y - 4, t.w + 8, t.h + 8, ht ? glm::vec3(1.0f, 0.85f, 0.4f) : glm::vec3(0.1f));
    drawRect(t.x, t.y, t.w, t.h, ht ? glm::vec3(0.78f, 0.55f, 0.22f) : glm::vec3(0.65f, 0.45f, 0.18f));
    drawTextC(t.x + t.w * 0.5f, t.y + 40, 3.0f, glm::vec3(1), "TERRORISTS", 1.0f, true);
    drawTextC(t.x + t.w * 0.5f, t.y + 130, 5.0f, glm::vec3(1), "T", 1.0f, true);
    drawTextC(t.x + t.w * 0.5f, t.y + 230, 2.0f, glm::vec3(1), "AK-47  GLOCK-18  C4", 0.9f);
    drawTextC(t.x + t.w * 0.5f, t.y + 260, 2.0f, glm::vec3(1), "PLANT THE BOMB", 0.9f);
    drawRect(ct.x - 4, ct.y - 4, ct.w + 8, ct.h + 8, hct ? glm::vec3(1.0f, 0.85f, 0.4f) : glm::vec3(0.1f));
    drawRect(ct.x, ct.y, ct.w, ct.h, hct ? glm::vec3(0.28f, 0.48f, 0.80f) : glm::vec3(0.20f, 0.38f, 0.65f));
    drawTextC(ct.x + ct.w * 0.5f, ct.y + 40, 2.0f, glm::vec3(1), "COUNTER-TERRORISTS", 1.0f, true);
    drawTextC(ct.x + ct.w * 0.5f, ct.y + 130, 5.0f, glm::vec3(1), "CT", 1.0f, true);
    drawTextC(ct.x + ct.w * 0.5f, ct.y + 230, 2.0f, glm::vec3(1), "M4A1  USP-S", 0.9f);
    drawTextC(ct.x + ct.w * 0.5f, ct.y + 260, 2.0f, glm::vec3(1), "DEFUSE THE BOMB", 0.9f);
}

void drawPauseMenu() {
    float W = (float)g_winW, H = (float)g_winH;
    drawRect(0, 0, W, H, glm::vec3(0.05f, 0.05f, 0.07f), 0.75f);
    drawTextC(W * 0.5f, H * 0.5f - 190, 6.0f, glm::vec3(1), "PAUSED", 1.0f, true);
    const char* labels[4] = { "RESUME", "CHANGE TEAM", "MAIN MENU", "QUIT" };
    glm::vec3 cols[4] = { glm::vec3(0.18f, 0.52f, 0.24f), glm::vec3(0.25f, 0.40f, 0.60f), glm::vec3(0.55f, 0.45f, 0.20f), glm::vec3(0.60f, 0.18f, 0.18f) };
    for (int i = 0; i < 4; i++) drawButton(pauseBtn(i), labels[i], cols[i], pointInRect(g_mouseX, g_mouseY, pauseBtn(i)));
}

void drawBuyMenu() {
    float W = (float)g_winW, H = (float)g_winH;
    drawRect(0, 0, W, H, glm::vec3(0.06f, 0.07f, 0.09f), 0.88f);
    drawRect(W * 0.5f - 400, 60, 800, 60, glm::vec3(0.12f, 0.14f, 0.18f), 0.95f);
    drawText(W * 0.5f - 380, 78, 4.0f, glm::vec3(1), "BUY MENU", 1.0f, true);
    drawTextR(W * 0.5f + 380, 78, 4.0f, glm::vec3(0.2f, 0.85f, 0.3f), "$" + std::to_string(g_money), 1.0f, true);
    drawTextC(W * 0.5f, 130, 2.0f, glm::vec3(0.7f), std::string(g_playerTeam == TEAM_T ? "TERRORISTS" : "COUNTER-TERRORISTS") + "   -   CLICK TO BUY   -   B / ESC TO CLOSE");

    for (int i = 0; i < 8; i++) {
        const BuyItem& it = BUY_ITEMS[i];
        Rect r = buyCard(i);
        bool teamOk = itemTeamOk(it), owned = itemOwned(it), afford = g_money >= it.price;
        bool usable = teamOk && !owned && afford;
        bool hover = pointInRect(g_mouseX, g_mouseY, r);
        glm::vec3 bg = usable ? glm::vec3(0.14f, 0.17f, 0.22f) : glm::vec3(0.10f, 0.11f, 0.13f);
        if (hover && usable) bg = glm::vec3(0.22f, 0.27f, 0.34f);
        drawRect(r.x, r.y, r.w, r.h, bg, 0.97f);
        drawRect(r.x, r.y + r.h - 5, r.w, 5, usable ? glm::vec3(0.85f, 0.55f, 0.15f) : glm::vec3(0.3f));
        float ns = textW(it.name, 3.0f) > r.w - 20 ? 2.0f : 3.0f;
        drawText(r.x + 12, r.y + 12, ns, usable ? glm::vec3(1) : glm::vec3(0.55f), it.name);
        drawText(r.x + 12, r.y + 48, 3.0f, afford ? glm::vec3(0.2f, 0.85f, 0.3f) : glm::vec3(0.75f, 0.2f, 0.2f), "$" + std::to_string(it.price));
        std::string tag;
        if (!teamOk) tag = (WEAPON_DB[it.weapon].team == TEAM_T) ? "T ONLY" : "CT ONLY";
        else if (owned) tag = "OWNED";
        else if (it.weapon >= 0) tag = std::string("DMG ") + std::to_string((int)WEAPON_DB[it.weapon].damage);
        else if (it.weapon == -2) tag = "ALL WEAPONS";
        drawText(r.x + 12, r.y + 82, 2.0f, owned ? glm::vec3(0.4f, 0.8f, 0.4f) : glm::vec3(0.6f), tag);
    }
}

// ----------------------------------------------------------------------------
// Callbacks
// ----------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow*, int w, int h) { g_fbW = w; g_fbH = h; glViewport(0, 0, w, h); }
void window_size_callback(GLFWwindow*, int w, int h) { g_winW = w; g_winH = h; }

void mouse_callback(GLFWwindow*, double xpos, double ypos) {
    if (g_state != STATE_PLAYING) return;
    if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
    float xo = (float)(xpos - lastX), yo = (float)(lastY - ypos);
    lastX = xpos; lastY = ypos;
    float sens = g_scoped ? mouseSensitivity * 0.35f : mouseSensitivity;
    yaw += xo * sens;
    pitch = glm::clamp(pitch + yo * sens, -89.0f, 89.0f);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int) {
    if (action != GLFW_PRESS) return;
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (g_state == STATE_MAIN_MENU) {
            if (pointInRect(mx, my, mainBtn(0))) g_state = STATE_TEAM_SELECT;
            else if (pointInRect(mx, my, mainBtn(1))) { g_playerTeam = TEAM_CT; startMatch(window, MAP_TRAINING); }
            else if (pointInRect(mx, my, mainBtn(2))) glfwSetWindowShouldClose(window, true);
        }
        else if (g_state == STATE_TEAM_SELECT) {
            if (pointInRect(mx, my, teamCard(0))) { g_playerTeam = TEAM_T;  startMatch(window, MAP_AZTEC); }
            else if (pointInRect(mx, my, teamCard(1))) { g_playerTeam = TEAM_CT; startMatch(window, MAP_AZTEC); }
        }
        else if (g_state == STATE_PAUSE_MENU) {
            if (pointInRect(mx, my, pauseBtn(0))) { g_state = STATE_PLAYING; firstMouse = true; glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED); }
            else if (pointInRect(mx, my, pauseBtn(1))) g_state = STATE_TEAM_SELECT;
            else if (pointInRect(mx, my, pauseBtn(2))) g_state = STATE_MAIN_MENU;
            else if (pointInRect(mx, my, pauseBtn(3))) glfwSetWindowShouldClose(window, true);
        }
        else if (g_state == STATE_BUY_MENU) {
            for (int i = 0; i < 8; i++) if (pointInRect(mx, my, buyCard(i))) buyItem(BUY_ITEMS[i]);
        }
    }
    if (g_state == STATE_PLAYING && button == GLFW_MOUSE_BUTTON_RIGHT && !g_dead) {
        if (g_inv[g_activeSlot].has && WEAPON_DB[g_inv[g_activeSlot].type].isSniper) g_scoped = !g_scoped;
    }
}

// ----------------------------------------------------------------------------
// MAIN
// ----------------------------------------------------------------------------
int main() {
    srand((unsigned)time(nullptr));
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = glfwCreateWindow(g_winW, g_winH, "CS2 Graphics Final - de_aztec", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetWindowSizeCallback(window, window_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;

    glfwGetWindowSize(window, &g_winW, &g_winH);
    glfwGetFramebufferSize(window, &g_fbW, &g_fbH);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glEnable(GL_MULTISAMPLE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    buildMeshes();
    g_worldProg = createProgram(WORLD_VS, WORLD_FS);
    g_unlitProg = createProgram(UNLIT_VS, UNLIT_FS);
    g_uiProg = createProgram(UI_VS, UI_FS);
    cacheLocations();
    initRain();
    resetBomb();
    for (int i = 0; i < SLOT_COUNT; i++) g_inv[i] = { WEAPON_KNIFE, 0, 0, false };

    float lastFrame = (float)glfwGetTime();
    bool escPrev = false, bPrev = false, enterPrev = false;

    while (!glfwWindowShouldClose(window)) {
        float now = (float)glfwGetTime();
        float dt = std::min(0.05f, now - lastFrame);
        lastFrame = now;

        glfwPollEvents();
        glfwGetCursorPos(window, &g_mouseX, &g_mouseY);
        if (g_winW <= 0 || g_winH <= 0) { glfwSwapBuffers(window); continue; }   // minimized
        g_uiProjection = glm::ortho(0.0f, (float)g_winW, (float)g_winH, 0.0f, -1.0f, 1.0f);

        // ---- keyboard shortcuts ----
        bool escDown = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (escDown && !escPrev) {
            if (g_state == STATE_PLAYING) { g_state = STATE_PAUSE_MENU; glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL); }
            else if (g_state == STATE_PAUSE_MENU || g_state == STATE_BUY_MENU) {
                g_state = STATE_PLAYING; firstMouse = true; glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            }
            else if (g_state == STATE_TEAM_SELECT) g_state = STATE_MAIN_MENU;
        }
        escPrev = escDown;

        bool bDown = glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS;
        if (bDown && !bPrev && (g_state == STATE_PLAYING || g_state == STATE_BUY_MENU)) {
            if (g_state == STATE_PLAYING && !g_dead) { g_state = STATE_BUY_MENU; glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL); }
            else if (g_state == STATE_BUY_MENU) { g_state = STATE_PLAYING; firstMouse = true; glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED); }
        }
        bPrev = bDown;
        (void)enterPrev;

        if (g_state == STATE_PLAYING && !g_dead) {
            for (int k = 0; k < SLOT_COUNT; k++) {
                if (glfwGetKey(window, GLFW_KEY_1 + k) == GLFW_PRESS && g_inv[k].has && g_activeSlot != k) {
                    g_activeSlot = k; g_scoped = false; g_reloadTimer = 0.0f; g_shotCooldown = 0.3f;
                }
            }
        }

        // ---- update ----
        if (g_state == STATE_PLAYING) updateGame(window, dt);

        // ---- render ----
        glViewport(0, 0, g_fbW, g_fbH);
        glClearColor(0.52f, 0.55f, 0.58f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        bool inGame = (g_state == STATE_PLAYING || g_state == STATE_PAUSE_MENU || g_state == STATE_BUY_MENU);
        if (inGame) renderWorld(now);

        glDisable(GL_DEPTH_TEST);
        if (inGame) drawHUD();
        if (g_state == STATE_BUY_MENU) drawBuyMenu();
        if (g_state == STATE_PAUSE_MENU) drawPauseMenu();
        if (g_state == STATE_TEAM_SELECT) drawTeamSelect();
        if (g_state == STATE_MAIN_MENU) drawMainMenu();
        glEnable(GL_DEPTH_TEST);

        glfwSwapBuffers(window);
    }

    glfwTerminate();
    return 0;
}