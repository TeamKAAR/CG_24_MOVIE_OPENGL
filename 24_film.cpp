/*
 * =============================================================================
 *   24: THE OPENGL CUT
 *   Written and Directed by: Karthik and Rohith
 *   Special Thanks to Ms. Alakananda K.
 *
 *   BUILD INSTRUCTIONS:
 *   Linux/Mac:
 *     g++ -std=c++17 -O2 24_the_opengl_cut.cpp -lGL -lGLU -lglut -o 24_film
 *     ./24_film
 *
 *   Windows (MinGW):
 *     g++ -std=c++17 -O2 24_the_opengl_cut.cpp -lopengl32 -lglu32 -lfreeglut -lgdiplus -o 24_film.exe
 *
 *   macOS (may need XQuartz or use GLFW port):
 *     g++ -std=c++17 -O2 24_the_opengl_cut.cpp -framework OpenGL -framework GLUT -o 24_film
 *
 *   DEPENDENCIES: OpenGL, GLU, GLUT (freeglut recommended)
 *   Install: sudo apt-get install freeglut3-dev   (Ubuntu/Debian)
 *            brew install freeglut                (Mac via Homebrew)
 *
 *   SCENES:
 *     Scene 0 – Title Card (logo + credits)
 *     Scene 1 – The Discovery (rainy street, Leo finds the watch)
 *     Scene 2 – The Watch Close-Up (macro gears)
 *     Scene 3 – CSK vs RCB (stadium, frozen time, ball redirect)
 *     Scene 4 – The Closed Loop (Leo places the book)
 *     Scene 5 – Post-Credit: Berlin 1945 (B&W bunker)
 *
 *   Press SPACE to advance / skip scenes manually.
 *   Scenes auto-advance based on their duration.
 * =============================================================================
 */

#ifdef __APPLE__
  #include <GLUT/glut.h>
#else
  #include <GL/glut.h>
#endif

#include <GL/glu.h>
#ifdef _WIN32
  #include <windows.h>
  #include <gdiplus.h>
#endif
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>

#ifdef _WIN32
using namespace Gdiplus;
#endif

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

// ─────────────────────────────────────────────────────────────────────────────
// GLOBAL CONSTANTS & HELPERS
// ─────────────────────────────────────────────────────────────────────────────

static const int W = 1280, H = 720;
static float gTime       = 0.0f;   // scene-local time (seconds)
static float gGlobalTime = 0.0f;   // total film time
static int   gScene      = 0;      // current scene index
static bool  gPaused     = false;
static GLuint gAwakeningBgTex = 0;
static GLuint gBerlinFlagTex  = 0;

#ifdef _WIN32
static ULONG_PTR gGdiToken = 0;
#endif

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float clamp01(float t)               { return t < 0 ? 0 : (t > 1 ? 1 : t); }
inline float smoothstep(float t)            { t = clamp01(t); return t*t*(3-2*t); }
inline float pulse(float t, float f)        { return 0.5f*(1+sinf(t*f*6.2831853f)); }
inline float frand()                        { return (float)rand()/(float)RAND_MAX; }
inline float frand(float lo, float hi)      { return lo + frand()*(hi-lo); }

// Scene durations (seconds)
static const float SCENE_DUR[] = {
    8.0f,   // 0 – Title
    14.0f,  // 1 – Discovery / Rain
    10.0f,  // 2 – Watch Macro
    18.0f,  // 3 – Stadium
    12.0f,  // 4 – Closed Loop
    12.0f,  // 5 – Berlin 1945
};
static const int NUM_SCENES = 6;

static const float FILM_SCENE_DUR[] = {
    25.0f,  // 0 - Title sequence
    60.0f,  // 1 - Discovery and rain
    40.0f,  // 2 - Watch macro / awakening
    95.0f,  // 3 - Stadium rescue
    45.0f,  // 4 - Closed loop
    35.0f,  // 5 - Berlin 1945
};

inline float sceneTimeScale(int sceneIndex) {
    if (sceneIndex < 0 || sceneIndex >= NUM_SCENES) return 1.0f;
    return SCENE_DUR[sceneIndex] / FILM_SCENE_DUR[sceneIndex];
}

inline float sceneStoryTime(int sceneIndex, float actualTime) {
    return actualTime * sceneTimeScale(sceneIndex);
}

inline float currentSceneStoryTime() {
    return sceneStoryTime(gScene, gTime);
}

inline float currentSceneStoryDt(float dt) {
    return dt * sceneTimeScale(gScene);
}

bool initTextureAssets() {
#ifdef _WIN32
    if (gGdiToken != 0) return true;
    GdiplusStartupInput input;
    return GdiplusStartup(&gGdiToken, &input, nullptr) == Ok;
#else
    return false;
#endif
}

void shutdownTextureAssets() {
#ifdef _WIN32
    if (gGdiToken != 0) {
        GdiplusShutdown(gGdiToken);
        gGdiToken = 0;
    }
#endif
}

bool loadTextureFromFile(const std::string& path, GLuint& outTex) {
#ifdef _WIN32
    if (!initTextureAssets()) return false;

    std::wstring wpath(path.begin(), path.end());
    Bitmap source(wpath.c_str(), FALSE);
    if (source.GetLastStatus() != Ok) return false;

    UINT width = source.GetWidth();
    UINT height = source.GetHeight();
    if (width == 0 || height == 0) return false;

    Bitmap converted(width, height, PixelFormat32bppARGB);
    Graphics graphics(&converted);
    graphics.DrawImage(&source, 0, 0, width, height);

    Rect rect(0, 0, (INT)width, (INT)height);
    BitmapData data = {};
    if (converted.LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, &data) != Ok) return false;

    std::vector<unsigned char> pixels(width * height * 4);
    const int stride = std::abs(data.Stride);
    for (UINT y = 0; y < height; ++y) {
        const unsigned char* srcRow = static_cast<const unsigned char*>(data.Scan0) + y * stride;
        unsigned char* dstRow = pixels.data() + (height - 1 - y) * width * 4;
        std::memcpy(dstRow, srcRow, width * 4);
    }
    converted.UnlockBits(&data);

    if (outTex == 0) glGenTextures(1, &outTex);
    glBindTexture(GL_TEXTURE_2D, outTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)width, (GLsizei)height, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
#else
    (void)path;
    (void)outTex;
    return false;
#endif
}

void initSceneTextures() {
    if (!loadTextureFromFile("assets/RCB_CSK.jpg", gAwakeningBgTex))
        std::printf("Warning: failed to load assets/RCB_CSK.jpg\n");
    if (!loadTextureFromFile("assets/Hitler_flag.png", gBerlinFlagTex))
        std::printf("Warning: failed to load assets/Hitler_flag.png\n");
}

void drawTexturedQuad2D(GLuint tex,
                        float x0, float y0,
                        float x1, float y1,
                        float alpha = 1.0f,
                        float u0 = 0.0f, float v0 = 0.0f,
                        float u1 = 1.0f, float v1 = 1.0f) {
    if (!tex) return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glColor4f(1.0f, 1.0f, 1.0f, alpha);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(x0, y0);
    glTexCoord2f(u1, v0); glVertex2f(x1, y0);
    glTexCoord2f(u1, v1); glVertex2f(x1, y1);
    glTexCoord2f(u0, v1); glVertex2f(x0, y1);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

// ─────────────────────────────────────────────────────────────────────────────
// STICKMAN / LEO  (articulated stick figure with torso + limbs)
// ─────────────────────────────────────────────────────────────────────────────

struct Joint { float x, y, z; };

void drawDiskXY(float x, float y, float z, float radius, int segments = 24) {
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(x, y, z);
    for (int i = 0; i <= segments; i++) {
        float a = i * 6.2831853f / segments;
        glVertex3f(x + cosf(a) * radius, y + sinf(a) * radius, z);
    }
    glEnd();
}

void drawCircleOutlineXY(float x, float y, float z, float radius, int segments = 24) {
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < segments; i++) {
        float a = i * 6.2831853f / segments;
        glVertex3f(x + cosf(a) * radius, y + sinf(a) * radius, z);
    }
    glEnd();
}

void drawRibbonSegment(const Joint& a, const Joint& b, float halfWidth) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-4f) return;

    float nx = -dy / len * halfWidth;
    float ny =  dx / len * halfWidth;
    float depth = std::max(0.04f, halfWidth * 0.85f);

    glBegin(GL_QUADS);
    glVertex3f(a.x + nx, a.y + ny, a.z + depth);
    glVertex3f(a.x - nx, a.y - ny, a.z + depth);
    glVertex3f(b.x - nx, b.y - ny, b.z + depth);
    glVertex3f(b.x + nx, b.y + ny, b.z + depth);

    glVertex3f(a.x - nx, a.y - ny, a.z - depth);
    glVertex3f(a.x + nx, a.y + ny, a.z - depth);
    glVertex3f(b.x + nx, b.y + ny, b.z - depth);
    glVertex3f(b.x - nx, b.y - ny, b.z - depth);

    glVertex3f(a.x + nx, a.y + ny, a.z + depth);
    glVertex3f(a.x + nx, a.y + ny, a.z - depth);
    glVertex3f(b.x + nx, b.y + ny, b.z - depth);
    glVertex3f(b.x + nx, b.y + ny, b.z + depth);

    glVertex3f(a.x - nx, a.y - ny, a.z - depth);
    glVertex3f(a.x - nx, a.y - ny, a.z + depth);
    glVertex3f(b.x - nx, b.y - ny, b.z + depth);
    glVertex3f(b.x - nx, b.y - ny, b.z - depth);
    glEnd();
}

void drawQuadExtruded(const Joint& a, const Joint& b, const Joint& c, const Joint& d, float depth) {
    glBegin(GL_QUADS);
    glVertex3f(a.x, a.y, a.z + depth);
    glVertex3f(b.x, b.y, b.z + depth);
    glVertex3f(c.x, c.y, c.z + depth);
    glVertex3f(d.x, d.y, d.z + depth);

    glVertex3f(d.x, d.y, d.z - depth);
    glVertex3f(c.x, c.y, c.z - depth);
    glVertex3f(b.x, b.y, b.z - depth);
    glVertex3f(a.x, a.y, a.z - depth);

    glVertex3f(a.x, a.y, a.z + depth);
    glVertex3f(a.x, a.y, a.z - depth);
    glVertex3f(b.x, b.y, b.z - depth);
    glVertex3f(b.x, b.y, b.z + depth);

    glVertex3f(b.x, b.y, b.z + depth);
    glVertex3f(b.x, b.y, b.z - depth);
    glVertex3f(c.x, c.y, c.z - depth);
    glVertex3f(c.x, c.y, c.z + depth);

    glVertex3f(c.x, c.y, c.z + depth);
    glVertex3f(c.x, c.y, c.z - depth);
    glVertex3f(d.x, d.y, d.z - depth);
    glVertex3f(d.x, d.y, d.z + depth);

    glVertex3f(d.x, d.y, d.z + depth);
    glVertex3f(d.x, d.y, d.z - depth);
    glVertex3f(a.x, a.y, a.z - depth);
    glVertex3f(a.x, a.y, a.z + depth);
    glEnd();
}

void drawTriangleExtruded(const Joint& a, const Joint& b, const Joint& c, float depth) {
    glBegin(GL_TRIANGLES);
    glVertex3f(a.x, a.y, a.z + depth);
    glVertex3f(b.x, b.y, b.z + depth);
    glVertex3f(c.x, c.y, c.z + depth);

    glVertex3f(c.x, c.y, c.z - depth);
    glVertex3f(b.x, b.y, b.z - depth);
    glVertex3f(a.x, a.y, a.z - depth);
    glEnd();

    glBegin(GL_QUADS);
    glVertex3f(a.x, a.y, a.z + depth);
    glVertex3f(a.x, a.y, a.z - depth);
    glVertex3f(b.x, b.y, b.z - depth);
    glVertex3f(b.x, b.y, b.z + depth);

    glVertex3f(b.x, b.y, b.z + depth);
    glVertex3f(b.x, b.y, b.z - depth);
    glVertex3f(c.x, c.y, c.z - depth);
    glVertex3f(c.x, c.y, c.z + depth);

    glVertex3f(c.x, c.y, c.z + depth);
    glVertex3f(c.x, c.y, c.z - depth);
    glVertex3f(a.x, a.y, a.z - depth);
    glVertex3f(a.x, a.y, a.z + depth);
    glEnd();
}

struct Stickman {
    float px, py, pz;       // root position
    float scale;
    float walkPhase;        // 0..2π
    bool  frozen;
    float heading;          // rotation about Y in degrees

    Joint head, torso0, torso1,
          lShoulder, lElbow, lHand,
          rShoulder, rElbow, rHand,
          lHip, lKnee, lFoot,
          rHip, rKnee, rFoot;

    void update(float dt) {
        if (!frozen) walkPhase += dt * 2.0f;
        computePose();
    }

    void computePose() {
        float w = frozen ? 0 : walkPhase;
        // Torso
        torso0 = {0, 1.5f, 0};
        torso1 = {0, 2.8f, 0};
        head   = {0, 3.35f, 0};
        // Shoulders / hips
        lShoulder = {-0.35f, 2.7f, 0};
        rShoulder = { 0.35f, 2.7f, 0};
        lHip      = {-0.20f, 1.5f, 0};
        rHip      = { 0.20f, 1.5f, 0};
        // Arms swing opposite to legs
        float armSwing = sinf(w) * 0.5f;
        lElbow = {lShoulder.x - 0.2f, lShoulder.y - 0.4f,  armSwing * 0.3f};
        lHand  = {lShoulder.x - 0.15f, lShoulder.y - 0.75f, armSwing * 0.5f};
        rElbow = {rShoulder.x + 0.2f, rShoulder.y - 0.4f, -armSwing * 0.3f};
        rHand  = {rShoulder.x + 0.15f, rShoulder.y - 0.75f, -armSwing * 0.5f};
        // Legs
        float legSwing = sinf(w) * 0.45f;
        lKnee = {lHip.x - 0.05f, lHip.y - 0.5f,  legSwing * 0.3f};
        lFoot = {lHip.x - 0.05f, lHip.y - 1.0f,  legSwing * 0.5f};
        rKnee = {rHip.x + 0.05f, rHip.y - 0.5f, -legSwing * 0.3f};
        rFoot = {rHip.x + 0.05f, rHip.y - 1.0f, -legSwing * 0.5f};
    }

    void draw(float r, float g, float b, float alpha = 1.0f) const {
        glPushMatrix();
        glTranslatef(px, py, pz);
        glRotatef(heading, 0, 1, 0);
        glScalef(scale, scale, scale);

        float hr = 0.24f;
        float outlineR = 0.06f;
        float outlineG = 0.05f;
        float outlineB = 0.05f;
        float clothR = std::max(0.08f, r * 0.30f);
        float clothG = std::max(0.08f, g * 0.28f);
        float clothB = std::max(0.12f, b * 0.36f);
        float hairR  = std::max(0.05f, clothR * 0.55f);
        float hairG  = std::max(0.05f, clothG * 0.55f);
        float hairB  = std::max(0.05f, clothB * 0.55f);
        float bodyDepth = 0.12f;

        Joint chestL = {lShoulder.x + 0.05f, lShoulder.y - 0.03f, 0.02f};
        Joint chestR = {rShoulder.x - 0.05f, rShoulder.y - 0.03f, 0.02f};
        Joint waistL = {lHip.x - 0.14f, lHip.y + 0.06f, 0.02f};
        Joint waistR = {rHip.x + 0.14f, rHip.y + 0.06f, 0.02f};
        Joint coatTail = {0.0f, 1.05f, 0.0f};

        // Soft shadow adds a little depth before the main figure.
        glColor4f(0.0f, 0.0f, 0.0f, alpha * 0.16f);
        glPushMatrix();
        glTranslatef(0.05f, -0.05f, -0.03f);
        drawDiskXY(head.x, head.y, head.z, hr + 0.02f, 28);
        glBegin(GL_POLYGON);
        glVertex3f(chestL.x, chestL.y, chestL.z);
        glVertex3f(chestR.x, chestR.y, chestR.z);
        glVertex3f(waistR.x, waistR.y, waistR.z);
        glVertex3f(waistL.x, waistL.y, waistL.z);
        glEnd();
        glPopMatrix();

        // Coat / torso
        glColor4f(clothR, clothG, clothB, alpha);
        drawQuadExtruded(chestL, chestR, waistR, waistL, bodyDepth);
        drawTriangleExtruded({waistL.x + 0.03f, waistL.y, waistL.z},
                             {waistR.x - 0.03f, waistR.y, waistR.z},
                             coatTail,
                             bodyDepth * 0.82f);

        // Shirt panel and lapels
        glColor4f(r * 0.92f, g * 0.94f, b * 0.98f, alpha * 0.92f);
        glBegin(GL_TRIANGLES);
        glVertex3f(0.0f, 2.55f, 0.04f);
        glVertex3f(-0.10f, 1.60f, 0.04f);
        glVertex3f(0.10f, 1.60f, 0.04f);
        glEnd();
        glColor4f(clothR * 0.70f, clothG * 0.70f, clothB * 0.70f, alpha);
        glBegin(GL_TRIANGLES);
        glVertex3f(-0.20f, 2.45f, 0.05f);
        glVertex3f(-0.02f, 2.10f, 0.05f);
        glVertex3f(-0.04f, 1.85f, 0.05f);
        glVertex3f(0.20f, 2.45f, 0.05f);
        glVertex3f(0.02f, 2.10f, 0.05f);
        glVertex3f(0.04f, 1.85f, 0.05f);
        glEnd();

        // Limbs with thickness.
        glColor4f(clothR * 0.95f, clothG * 0.95f, clothB * 1.02f, alpha);
        drawRibbonSegment(lShoulder, lElbow, 0.06f);
        drawRibbonSegment(rShoulder, rElbow, 0.06f);
        drawRibbonSegment(lElbow, lHand, 0.05f);
        drawRibbonSegment(rElbow, rHand, 0.05f);
        drawRibbonSegment(lHip, lKnee, 0.07f);
        drawRibbonSegment(rHip, rKnee, 0.07f);
        drawRibbonSegment(lKnee, lFoot, 0.06f);
        drawRibbonSegment(rKnee, rFoot, 0.06f);

        // Neck
        glColor4f(r * 0.86f, g * 0.80f, b * 0.76f, alpha);
        drawQuadExtruded({-0.05f, 2.95f, 0.02f},
                         { 0.05f, 2.95f, 0.02f},
                         { 0.04f, 2.78f, 0.02f},
                         {-0.04f, 2.78f, 0.02f},
                         0.06f);

        // Head
        glColor4f(r * 0.96f, g * 0.90f, b * 0.84f, alpha);
        glPushMatrix();
        glTranslatef(head.x, head.y, head.z + 0.01f);
        glScalef(1.0f, 1.08f, 0.76f);
        glutSolidSphere(hr, 20, 16);
        glPopMatrix();
        glColor4f(r, g, b, alpha * 0.35f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(head.x, head.y - 0.03f, head.z + 0.03f);
        for (int i = 0; i <= 16; i++) {
            float a = 3.1415926f + i * 3.1415926f / 16.0f;
            glVertex3f(head.x + cosf(a) * hr * 0.90f,
                       head.y + sinf(a) * hr * 0.95f - 0.04f,
                       head.z + 0.03f);
        }
        glEnd();

        // Hair cap
        glColor4f(hairR, hairG, hairB, alpha);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(head.x, head.y + 0.06f, head.z + 0.04f);
        for (int i = 0; i <= 18; i++) {
            float a = 0.15f + i * 2.85f / 18.0f;
            glVertex3f(head.x + cosf(a) * hr * 0.92f,
                       head.y + sinf(a) * hr * 0.88f + 0.04f,
                       head.z + 0.04f);
        }
        glEnd();

        // Face details
        glColor4f(outlineR, outlineG, outlineB, alpha * 0.85f);
        glLineWidth(2.0f);
        glBegin(GL_LINES);
        glVertex3f(head.x - 0.10f, head.y + 0.05f, head.z + 0.05f);
        glVertex3f(head.x - 0.04f, head.y + 0.04f, head.z + 0.05f);
        glVertex3f(head.x + 0.04f, head.y + 0.04f, head.z + 0.05f);
        glVertex3f(head.x + 0.10f, head.y + 0.05f, head.z + 0.05f);
        glVertex3f(head.x + 0.01f, head.y + 0.01f, head.z + 0.05f);
        glVertex3f(head.x + 0.03f, head.y - 0.05f, head.z + 0.05f);
        glVertex3f(head.x - 0.06f, head.y - 0.10f, head.z + 0.05f);
        glVertex3f(head.x + 0.06f, head.y - 0.11f, head.z + 0.05f);
        glEnd();

        // Hands and shoes
        glColor4f(r * 0.95f, g * 0.88f, b * 0.82f, alpha);
        glPushMatrix();
        glTranslatef(lHand.x, lHand.y, lHand.z + 0.02f);
        glScalef(1.0f, 1.0f, 0.8f);
        glutSolidSphere(0.07f, 14, 12);
        glPopMatrix();
        glPushMatrix();
        glTranslatef(rHand.x, rHand.y, rHand.z + 0.02f);
        glScalef(1.0f, 1.0f, 0.8f);
        glutSolidSphere(0.07f, 14, 12);
        glPopMatrix();
        glColor4f(clothR * 0.45f, clothG * 0.45f, clothB * 0.50f, alpha);
        drawQuadExtruded({lFoot.x - 0.12f, lFoot.y - 0.04f, lFoot.z + 0.02f},
                         {lFoot.x + 0.05f, lFoot.y - 0.04f, lFoot.z + 0.02f},
                         {lFoot.x + 0.10f, lFoot.y - 0.14f, lFoot.z + 0.02f},
                         {lFoot.x - 0.08f, lFoot.y - 0.14f, lFoot.z + 0.02f},
                         0.06f);
        drawQuadExtruded({rFoot.x - 0.05f, rFoot.y - 0.04f, rFoot.z + 0.02f},
                         {rFoot.x + 0.12f, rFoot.y - 0.04f, rFoot.z + 0.02f},
                         {rFoot.x + 0.08f, rFoot.y - 0.14f, rFoot.z + 0.02f},
                         {rFoot.x - 0.10f, rFoot.y - 0.14f, rFoot.z + 0.02f},
                         0.06f);

        // Joints and outline
        glColor4f(clothR * 0.82f, clothG * 0.82f, clothB * 0.86f, alpha);
        auto jointBall = [&](const Joint& j, float radius) {
            glPushMatrix();
            glTranslatef(j.x, j.y, j.z + 0.01f);
            glScalef(1.0f, 1.0f, 0.8f);
            glutSolidSphere(radius, 14, 12);
            glPopMatrix();
        };
        jointBall(lShoulder, 0.055f);
        jointBall(rShoulder, 0.055f);
        jointBall(lElbow, 0.05f);
        jointBall(rElbow, 0.05f);
        jointBall(lKnee, 0.055f);
        jointBall(rKnee, 0.055f);

        glColor4f(outlineR, outlineG, outlineB, alpha * 0.65f);
        glLineWidth(1.6f);
        glBegin(GL_LINE_STRIP);
        glVertex3f(lShoulder.x, lShoulder.y, lShoulder.z + 0.06f);
        glVertex3f(0.0f, 2.84f, 0.06f);
        glVertex3f(rShoulder.x, rShoulder.y, rShoulder.z + 0.06f);
        glVertex3f(waistR.x, waistR.y, waistR.z + 0.06f);
        glVertex3f(coatTail.x, coatTail.y, coatTail.z + 0.06f);
        glVertex3f(waistL.x, waistL.y, waistL.z + 0.06f);
        glVertex3f(lShoulder.x, lShoulder.y, lShoulder.z + 0.06f);
        glEnd();
        drawCircleOutlineXY(head.x, head.y, head.z + 0.05f, hr, 30);

        glPopMatrix();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// RAIN PARTICLE SYSTEM
// ─────────────────────────────────────────────────────────────────────────────

struct RainDrop {
    float x, y, z, vy;
    float len;
};

static const int NUM_DROPS = 800;
static RainDrop drops[NUM_DROPS];
static bool rainFrozen = false;

void initRain() {
    for (auto& d : drops) {
        d.x   = frand(-12, 12);
        d.y   = frand(-2, 14);
        d.z   = frand(-8, 2);
        d.vy  = frand(-8.0f, -12.0f);
        d.len = frand(0.15f, 0.4f);
    }
}

void updateRain(float dt, float timeMult = 1.0f) {
    if (rainFrozen) return;
    for (auto& d : drops) {
        d.y += d.vy * dt * timeMult;
        if (d.y < -2.0f) { d.y = 14.0f; d.x = frand(-12, 12); }
    }
}

void drawRain(float alpha = 1.0f, bool rewinding = false) {
    glLineWidth(1.5f);
    glBegin(GL_LINES);
    for (const auto& d : drops) {
        glColor4f(0.55f, 0.7f, 0.9f, alpha * 0.6f);
        glVertex3f(d.x, d.y, d.z);
        glColor4f(0.55f, 0.7f, 0.9f, 0.0f);
        glVertex3f(d.x + (rewinding ? -0.08f : 0.08f), d.y + (rewinding ? -d.len : d.len), d.z);
    }
    glEnd();
}

// ─────────────────────────────────────────────────────────────────────────────
// WATCH GEAR SYSTEM (macro view)
// ─────────────────────────────────────────────────────────────────────────────

struct Gear {
    float cx, cy;       // centre
    float outerR, innerR;
    int   teeth;
    float angle;        // degrees
    float speed;        // deg/s (can be negative)
    float r, g, b;
    float z;
};

static std::vector<Gear> gears;

void initGears() {
    gears.clear();
    // Main escapement wheel (large, centre)
    gears.push_back({0,   0,    1.1f, 0.75f, 20,  0,  45.0f, 0.80f, 0.65f, 0.35f, 0.0f});
    // Second wheel
    gears.push_back({1.9f, 0.4f, 0.7f, 0.45f, 14,  0, -90.0f, 0.70f, 0.55f, 0.30f, 0.05f});
    // Third wheel
    gears.push_back({-1.7f,-0.5f,0.6f, 0.38f, 12,  0,  70.0f, 0.75f, 0.60f, 0.25f, 0.02f});
    // Pinion
    gears.push_back({0.8f,-1.5f, 0.4f, 0.25f, 8,   0, 120.0f, 0.85f, 0.70f, 0.40f, -0.02f});
    // Crown wheel
    gears.push_back({-0.5f, 1.6f,0.5f, 0.30f, 10,  0, -60.0f, 0.78f, 0.63f, 0.28f, 0.03f});
}

void drawGear(const Gear& ge, float timeWarp = 1.0f) {
    float a = ge.angle * timeWarp;
    glPushMatrix();
    glTranslatef(ge.cx, ge.cy, ge.z);
    glRotatef(a, 0, 0, 1);

    int T = ge.teeth;
    float toothH = (ge.outerR - ge.innerR) * 0.5f;
    float step = 360.0f / T;

    // Gear body
    glColor4f(ge.r, ge.g, ge.b, 1.0f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(0, 0, 0);
    for (int i = 0; i <= T*4; i++) {
        float ang = i * 6.2831853f / (T * 4);
        glVertex3f(ge.innerR * cosf(ang), ge.innerR * sinf(ang), 0);
    }
    glEnd();

    // Teeth
    for (int i = 0; i < T; i++) {
        float a0 = i * step * 3.14159265f / 180.0f;
        float a1 = a0 + step * 0.4f * 3.14159265f / 180.0f;
        float am = (a0 + a1) * 0.5f;

        glColor4f(ge.r * 1.1f, ge.g * 1.1f, ge.b * 1.1f, 1.0f);
        glBegin(GL_TRIANGLE_STRIP);
        glVertex3f(ge.innerR * cosf(a0), ge.innerR * sinf(a0), 0);
        glVertex3f(ge.outerR * cosf(a0 + toothH*0.2f), ge.outerR * sinf(a0 + toothH*0.2f), 0);
        glVertex3f(ge.innerR * cosf(a1), ge.innerR * sinf(a1), 0);
        glVertex3f(ge.outerR * cosf(am), ge.outerR * sinf(am), 0);
        glEnd();
    }

    // Axle
    glColor4f(0.9f, 0.8f, 0.5f, 1.0f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(0, 0, 0);
    for (int i = 0; i <= 16; i++) {
        float ang = i * 6.2831853f / 16.0f;
        glVertex3f(ge.innerR * 0.18f * cosf(ang), ge.innerR * 0.18f * sinf(ang), 0);
    }
    glEnd();

    // Edge highlight
    glColor4f(1.0f, 0.9f, 0.6f, 0.5f);
    glLineWidth(1.5f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 64; i++) {
        float ang = i * 6.2831853f / 64.0f;
        glVertex3f(ge.innerR * cosf(ang), ge.innerR * sinf(ang), 0);
    }
    glEnd();

    glPopMatrix();
}

void updateGears(float dt, float speed = 1.0f) {
    for (auto& ge : gears)
        ge.angle += ge.speed * dt * speed;
}

// ─────────────────────────────────────────────────────────────────────────────
// BLOOM EFFECT (fake, via additive blending layers)
// ─────────────────────────────────────────────────────────────────────────────

void drawGlow(float x, float y, float radius, float r, float g, float b, float alpha) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    for (int ring = 5; ring >= 1; ring--) {
        float fr = ring / 5.0f;
        glColor4f(r, g, b, alpha * (1.0f - fr) * 0.3f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(x, y, 0.1f);
        for (int i = 0; i <= 32; i++) {
            float a = i * 6.2831853f / 32.0f;
            glVertex3f(x + cosf(a) * radius * fr * 1.5f, y + sinf(a) * radius * fr * 1.5f, 0.1f);
        }
        glEnd();
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

// ─────────────────────────────────────────────────────────────────────────────
// TEXT RENDERING  (GLUT bitmap)
// ─────────────────────────────────────────────────────────────────────────────

void drawText(float x, float y, const std::string& s, float r, float g, float b, float a = 1.0f, void* font = GLUT_BITMAP_HELVETICA_18) {
    glColor4f(r, g, b, a);
    glRasterPos2f(x, y);
    for (char c : s) glutBitmapCharacter(font, c);
}

int bitmapTextWidth(const std::string& s, void* font = GLUT_BITMAP_HELVETICA_18) {
    int width = 0;
    for (unsigned char c : s) width += glutBitmapWidth(font, c);
    return width;
}

float strokeTextWidth(const std::string& s, void* font = GLUT_STROKE_ROMAN) {
    float width = 0.0f;
    for (unsigned char c : s) width += (float)glutStrokeWidth(font, c);
    return width;
}

void drawTextCentered(float cx, float y, const std::string& s, float r, float g, float b, float a = 1.0f, void* font = GLUT_BITMAP_HELVETICA_18) {
    drawText(cx - bitmapTextWidth(s, font) * 0.5f, y, s, r, g, b, a, font);
}

void drawTextBig(float x, float y, const std::string& s, float r, float g, float b, float a = 1.0f) {
    drawText(x, y, s, r, g, b, a, GLUT_BITMAP_TIMES_ROMAN_24);
}

void drawStrokeText(float x, float y, const std::string& s, float scale, float r, float g, float b, float a = 1.0f, void* font = GLUT_STROKE_ROMAN) {
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glScalef(scale, scale, 1.0f);
    glColor4f(r, g, b, a);
    for (char c : s) glutStrokeCharacter(font, c);
    glPopMatrix();
}

void drawStrokeTextCentered(float cx, float y, const std::string& s, float scale, float r, float g, float b, float a = 1.0f, void* font = GLUT_STROKE_ROMAN) {
    drawStrokeText(cx - strokeTextWidth(s, font) * scale * 0.5f, y, s, scale, r, g, b, a, font);
}

void drawRing2D(float cx, float cy, float innerR, float outerR, int segments = 96) {
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= segments; i++) {
        float a = i * 6.2831853f / segments;
        float cs = cosf(a);
        float sn = sinf(a);
        glVertex2f(cx + cs * outerR, cy + sn * outerR);
        glVertex2f(cx + cs * innerR, cy + sn * innerR);
    }
    glEnd();
}

void begin2D();
void end2D();

void drawSceneBanner(const std::string& sceneTag, const std::string& title, float alpha = 1.0f) {
    float cx = W * 0.5f;
    glColor4f(0.0f, 0.0f, 0.0f, 0.30f * alpha);
    glBegin(GL_QUADS);
    glVertex2f(cx - 220.0f, H - 62.0f);
    glVertex2f(cx + 220.0f, H - 62.0f);
    glVertex2f(cx + 220.0f, H - 18.0f);
    glVertex2f(cx - 220.0f, H - 18.0f);
    glEnd();

    drawTextCentered(cx, H - 36.0f, sceneTag, 0.78f, 0.78f, 0.80f, alpha * 0.85f, GLUT_BITMAP_HELVETICA_12);
    drawTextCentered(cx, H - 54.0f, title, 0.96f, 0.92f, 0.84f, alpha, GLUT_BITMAP_HELVETICA_18);
}

void drawMacroWatchShot(const std::string& sceneTag,
                        const std::string& sceneTitle,
                        float phase,
                        const std::string& line1,
                        const std::string& line2,
                        float accentR,
                        float accentG,
                        float accentB) {
    float p = clamp01(phase);
    float reverse = smoothstep(clamp01((p - 0.38f) / 0.18f));
    float crownPull = smoothstep(clamp01((p - 0.12f) / 0.16f)) * 0.42f;
    float clickAlpha = smoothstep(clamp01((p - 0.16f) / 0.06f)) * (1.0f - smoothstep(clamp01((p - 0.34f) / 0.08f)));
    float glowPulse = 0.78f + 0.22f * pulse(p, 0.9f);

    glClearColor(0.01f, 0.01f, 0.02f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-4.6f, 4.6f, -2.7f, 2.7f, -10.0f, 10.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBegin(GL_QUADS);
    glColor4f(0.01f, 0.01f, 0.02f, 1.0f);
    glVertex3f(-5.0f,  3.0f, -2.0f);
    glVertex3f( 5.0f,  3.0f, -2.0f);
    glColor4f(0.05f * accentR, 0.05f * accentG, 0.08f * accentB, 1.0f);
    glVertex3f( 5.0f, -3.0f, -2.0f);
    glVertex3f(-5.0f, -3.0f, -2.0f);
    glEnd();

    drawGlow(0.0f, 0.0f, 2.7f, accentR, accentG, accentB, 0.22f * glowPulse);
    drawGlow(0.0f, 0.0f, 1.8f, accentR, accentG, accentB, 0.14f * (0.5f + reverse * 0.8f));

    Gear mainGear  = { 0.0f,  0.0f, 1.65f, 1.08f, 26, lerp(p * 55.0f, -p * 260.0f - 30.0f, reverse), 0.0f, 0.72f, 0.58f, 0.28f, 0.0f };
    Gear leftGear  = {-1.55f,-0.25f,0.82f, 0.52f, 16, lerp(-p * 70.0f,  p * 320.0f + 30.0f, reverse), 0.0f, 0.64f, 0.50f, 0.26f, 0.0f };
    Gear rightGear = { 1.35f, 0.62f,0.66f, 0.40f, 14, lerp(p * 95.0f, -p * 360.0f - 15.0f, reverse), 0.0f, 0.80f, 0.65f, 0.32f, 0.0f };
    drawGear(mainGear, 1.0f);
    drawGear(leftGear, 1.0f);
    drawGear(rightGear, 1.0f);

    glColor4f(0.80f, 0.66f, 0.34f, 1.0f);
    glLineWidth(9.0f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 80; i++) {
        float a = i * 6.2831853f / 80.0f;
        glVertex3f(cosf(a) * 2.02f, sinf(a) * 2.02f, 0.18f);
    }
    glEnd();

    glColor4f(0.10f, 0.10f, 0.12f, 0.96f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(0.0f, 0.0f, 0.16f);
    for (int i = 0; i <= 80; i++) {
        float a = i * 6.2831853f / 80.0f;
        glVertex3f(cosf(a) * 1.78f, sinf(a) * 1.78f, 0.16f);
    }
    glEnd();

    glColor4f(0.95f, 0.90f, 0.78f, 0.92f);
    glLineWidth(3.0f);
    for (int i = 0; i < 12; i++) {
        float a = i * 6.2831853f / 12.0f;
        float r0 = (i % 3 == 0) ? 1.32f : 1.45f;
        glBegin(GL_LINES);
        glVertex3f(cosf(a) * r0, sinf(a) * r0, 0.18f);
        glVertex3f(cosf(a) * 1.62f, sinf(a) * 1.62f, 0.18f);
        glEnd();
    }

    glColor4f(0.76f, 0.62f, 0.30f, 1.0f);
    glPushMatrix();
    glTranslatef(2.05f + crownPull, 0.0f, 0.15f);
    glScalef(0.46f, 0.22f, 1.0f);
    glutSolidCube(1.0f);
    glPopMatrix();

    glColor4f(0.86f, 0.72f, 0.58f, 0.92f);
    glBegin(GL_POLYGON);
    glVertex3f(4.3f, -0.45f, 0.32f);
    glVertex3f(2.90f + crownPull, -0.42f, 0.32f);
    glVertex3f(2.55f + crownPull, -0.14f, 0.32f);
    glVertex3f(2.78f + crownPull,  0.18f, 0.32f);
    glVertex3f(4.3f, 0.26f, 0.32f);
    glEnd();
    glColor4f(0.94f, 0.82f, 0.68f, 0.45f);
    glBegin(GL_QUADS);
    glVertex3f(3.2f, -0.26f, 0.34f);
    glVertex3f(4.3f, -0.26f, 0.34f);
    glVertex3f(4.3f, -0.06f, 0.34f);
    glVertex3f(3.2f, -0.06f, 0.34f);
    glEnd();

    glPushMatrix();
    glTranslatef(0.0f, 0.0f, 0.24f);
    float handAngle = lerp(p * 55.0f, -p * 340.0f - 40.0f, reverse);
    auto macroHand = [&](float angleDeg, float len, float width, float r, float g, float b) {
        glPushMatrix();
        glRotatef(-angleDeg + 90.0f, 0, 0, 1);
        glLineWidth(width);
        glColor4f(r, g, b, 1.0f);
        glBegin(GL_LINES);
        glVertex3f(0.0f, -0.20f, 0.0f);
        glVertex3f(0.0f,  len,   0.0f);
        glEnd();
        glPopMatrix();
    };
    macroHand(handAngle * 0.083f, 0.86f, 7.0f, 0.94f, 0.88f, 0.76f);
    macroHand(handAngle,          1.26f, 5.0f, 0.94f, 0.88f, 0.76f);
    macroHand(handAngle * 8.0f,   1.45f, 2.2f, accentR, accentG, accentB);
    glColor4f(accentR, accentG, accentB, 1.0f);
    drawDiskXY(0.0f, 0.0f, 0.01f, 0.12f, 20);
    glPopMatrix();

    glColor4f(1.0f, 1.0f, 1.0f, 0.10f);
    glBegin(GL_QUADS);
    glVertex3f(-1.15f,  1.25f, 0.28f);
    glVertex3f( 1.55f,  1.65f, 0.28f);
    glVertex3f( 1.78f,  1.18f, 0.28f);
    glVertex3f(-0.95f,  0.82f, 0.28f);
    glEnd();

    begin2D();
    drawSceneBanner(sceneTag, sceneTitle, 1.0f);
    drawTextCentered(W * 0.5f, 96.0f, line1, 0.96f, 0.92f, 0.86f, 0.95f, GLUT_BITMAP_HELVETICA_18);
    drawTextCentered(W * 0.5f, 70.0f, line2, 0.82f, 0.86f, 0.92f, 0.95f, GLUT_BITMAP_HELVETICA_12);
    if (clickAlpha > 0.01f) {
        drawStrokeTextCentered(W * 0.79f, H * 0.62f, "CLICK", 0.28f, 1.0f, 0.92f, 0.76f, clickAlpha);
    }
    if (reverse > 0.05f) {
        drawTextCentered(W * 0.5f, 42.0f, "Needles reversing. Crown fully pulled.", accentR, accentG, accentB, 0.68f + 0.24f * reverse, GLUT_BITMAP_HELVETICA_12);
    }
    end2D();
}

// 2D ortho overlay
void begin2D() {
    glMatrixMode(GL_PROJECTION);
    glPushMatrix(); glLoadIdentity();
    gluOrtho2D(0, W, 0, H);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix(); glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
}
void end2D() {
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);  glPopMatrix();
}

// ─────────────────────────────────────────────────────────────────────────────
// ENVIRONMENT: STREET (Scene 1 & 4)
// ─────────────────────────────────────────────────────────────────────────────

void drawStreet(float wetness = 1.0f) {
    // Ground (wet = reflective dark)
    float gd = lerp(0.25f, 0.12f, wetness);
    glColor4f(gd, gd, gd + 0.04f, 1.0f);
    glBegin(GL_QUADS);
    glVertex3f(-20, -0.01f, -20); glVertex3f( 20, -0.01f, -20);
    glVertex3f( 20, -0.01f,  5);  glVertex3f(-20, -0.01f,  5);
    glEnd();

    // Pavement reflective highlight
    if (wetness > 0.3f) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glColor4f(0.4f, 0.5f, 0.8f, wetness * 0.15f);
        glBegin(GL_QUADS);
        glVertex3f(-8, 0, -15); glVertex3f(8, 0, -15);
        glVertex3f(8, 0, 5);    glVertex3f(-8, 0, 5);
        glEnd();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    // Kerbs
    glColor4f(0.35f, 0.35f, 0.35f, 1.0f);
    glBegin(GL_QUADS);
    glVertex3f(-8, 0.0f, -15); glVertex3f(-7.5f, 0.0f, -15);
    glVertex3f(-7.5f, 0.1f, 5); glVertex3f(-8,   0.1f, 5);
    glEnd();
    glBegin(GL_QUADS);
    glVertex3f( 7.5f, 0.0f, -15); glVertex3f(8, 0.0f, -15);
    glVertex3f( 8,    0.1f, 5);   glVertex3f(7.5f, 0.1f, 5);
    glEnd();

    // Buildings (low-poly blocks)
    auto building = [](float x, float z, float w, float h, float d, float r, float g, float b) {
        glColor4f(r, g, b, 1.0f);
        // Front face
        glBegin(GL_QUADS);
        glVertex3f(x,   0, z);   glVertex3f(x+w, 0, z);
        glVertex3f(x+w, h, z);   glVertex3f(x,   h, z);
        glEnd();
        // Side face
        glColor4f(r*0.7f, g*0.7f, b*0.7f, 1.0f);
        glBegin(GL_QUADS);
        glVertex3f(x+w, 0, z);   glVertex3f(x+w, 0, z+d);
        glVertex3f(x+w, h, z+d); glVertex3f(x+w, h, z);
        glEnd();
        // Windows (bright dots)
        glColor4f(1.0f, 0.95f, 0.6f, 0.9f);
        for (int row = 1; row <= (int)(h/1.5f); row++) {
            for (int col = 0; col < (int)(w/1.2f); col++) {
                float wx = x + 0.5f + col * 1.2f;
                float wy = row * 1.3f;
                if (wx < x + w - 0.3f) {
                    glBegin(GL_QUADS);
                    glVertex3f(wx,       wy,       z-0.01f);
                    glVertex3f(wx+0.5f,  wy,       z-0.01f);
                    glVertex3f(wx+0.5f,  wy+0.65f, z-0.01f);
                    glVertex3f(wx,       wy+0.65f, z-0.01f);
                    glEnd();
                }
            }
        }
    };

    building(-18, -14, 5, 9, 3, 0.20f, 0.20f, 0.22f);
    building(-12, -14, 4, 7, 3, 0.22f, 0.21f, 0.23f);
    building(  8, -14, 5, 10,3, 0.18f, 0.18f, 0.20f);
    building( 13, -14, 4, 6, 3, 0.23f, 0.22f, 0.24f);

    // Street lamp
    auto lamp = [](float x, float z) {
        glColor4f(0.3f, 0.3f, 0.3f, 1.0f);
        glLineWidth(3.0f);
        glBegin(GL_LINES);
        glVertex3f(x, 0, z); glVertex3f(x, 4.5f, z);
        glVertex3f(x, 4.5f, z); glVertex3f(x + 0.7f, 4.5f, z);
        glEnd();
        // bulb glow
        drawGlow(x + 0.7f, 4.5f, 0.4f, 1.0f, 0.95f, 0.7f, 0.8f);
    };
    lamp(-6.5f, -5.0f);
    lamp( 6.5f, -5.0f);
    lamp(-6.5f, -12.0f);
    lamp( 6.5f, -12.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// ENVIRONMENT: STADIUM (Scene 3)
// ─────────────────────────────────────────────────────────────────────────────

void drawStadium(float flashAlpha) {
    // Pitch (green rectangle)
    glColor4f(0.15f, 0.42f, 0.15f, 1.0f);
    glBegin(GL_QUADS);
    glVertex3f(-12, 0, -25); glVertex3f(12, 0, -25);
    glVertex3f(12, 0, 5);    glVertex3f(-12, 0, 5);
    glEnd();
    // Crease lines
    glColor4f(1, 1, 1, 0.8f);
    glLineWidth(2);
    glBegin(GL_LINES);
    glVertex3f(-1.22f, 0.01f, -3); glVertex3f(-1.22f, 0.01f, 3);
    glVertex3f( 1.22f, 0.01f, -3); glVertex3f( 1.22f, 0.01f, 3);
    // Bowling crease
    glVertex3f(-1.5f, 0.01f, -20); glVertex3f(1.5f, 0.01f, -20);
    glEnd();
    // Pitch strip
    glColor4f(0.6f, 0.5f, 0.3f, 1.0f);
    glBegin(GL_QUADS);
    glVertex3f(-1.22f, 0.01f, -20); glVertex3f(1.22f, 0.01f, -20);
    glVertex3f(1.22f, 0.01f, 3);    glVertex3f(-1.22f, 0.01f, 3);
    glEnd();

    // Stands (colorful crowd blobs, DoF faked with alpha)
    auto stand = [&](float x1, float z1, float x2, float z2, float h,
                      float sr, float sg, float sb) {
        glColor4f(sr, sg, sb, 0.7f);
        glBegin(GL_QUADS);
        glVertex3f(x1, 0, z1); glVertex3f(x2, 0, z1);
        glVertex3f(x2, h, z1); glVertex3f(x1, h, z1);
        glEnd();
        // flickering camera flash patches
        if (flashAlpha > 0.01f) {
            for (int i = 0; i < 20; i++) {
                float fx = x1 + frand() * (x2 - x1);
                float fy = frand() * h;
                glColor4f(1, 1, 1, flashAlpha * frand() * 0.8f);
                glPointSize(3);
                glBegin(GL_POINTS);
                glVertex3f(fx, fy, z1);
                glEnd();
            }
        }
    };

    // Four stands
    stand(-22, -25,  -12, 5,  12,  0.30f, 0.15f, 0.15f);  // left
    stand(  12, -25,   22, 5,  12,  0.20f, 0.20f, 0.35f);  // right
    stand(-12, -25,   12, -25, 12,  0.25f, 0.25f, 0.10f);  // far end
    // Stadium floodlights
    drawGlow(-18, 12, 2.5f, 1.0f, 0.97f, 0.8f, 0.6f);
    drawGlow( 18, 12, 2.5f, 1.0f, 0.97f, 0.8f, 0.6f);
    drawGlow(  0, 14, 2.0f, 1.0f, 0.97f, 0.8f, 0.5f);

    // Scoreboard
    glColor4f(0.05f, 0.05f, 0.05f, 1.0f);
    glBegin(GL_QUADS);
    glVertex3f(-4, 7, -24.5f); glVertex3f(4, 7, -24.5f);
    glVertex3f(4, 11, -24.5f); glVertex3f(-4, 11, -24.5f);
    glEnd();
    // Score text drawn in 2D overlay (handled in scene draw)
}

// ─────────────────────────────────────────────────────────────────────────────
// CRICKET BALL
// ─────────────────────────────────────────────────────────────────────────────

struct Ball {
    float x, y, z;
    float vx, vy, vz;
    bool  frozen;

    void update(float dt) {
        if (frozen) return;
        vy += -9.8f * dt * 0.3f;
        x += vx * dt;
        y += vy * dt;
        z += vz * dt;
    }

    void draw() const {
        glPushMatrix();
        glTranslatef(x, y, z);
        // Red ball
        glColor4f(0.85f, 0.10f, 0.08f, 1.0f);
        glutSolidSphere(0.18f, 12, 8);
        // Seam
        glColor4f(0.9f, 0.85f, 0.7f, 1.0f);
        glLineWidth(2);
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < 24; i++) {
            float a = i * 6.2831853f / 24.0f;
            glVertex3f(cosf(a)*0.18f, sinf(a)*0.18f, 0);
        }
        glEnd();
        // Glow trail
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glColor4f(1.0f, 0.4f, 0.1f, 0.3f);
        glutSolidSphere(0.35f, 8, 5);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glPopMatrix();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// WATCH FACE (small, worn on wrist)
// ─────────────────────────────────────────────────────────────────────────────

void drawWatchFace(float x, float y, float z, float hours, float minutes, float seconds, float bw = 0.0f) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glScalef(0.3f, 0.3f, 0.3f);

    // Case
    float cr = lerp(0.75f, 0.4f, bw);
    float cg = lerp(0.65f, 0.4f, bw);
    float cb = lerp(0.30f, 0.4f, bw);
    glColor4f(cr, cg, cb, 1.0f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(0, 0, 0);
    for (int i = 0; i <= 32; i++) {
        float a = i * 6.2831853f / 32;
        glVertex3f(cosf(a)*1.1f, sinf(a)*1.1f, 0);
    }
    glEnd();

    // Dial
    float dv = lerp(0.12f, 0.08f, bw);
    glColor4f(dv, dv, dv + lerp(0.04f, 0, bw), 1.0f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(0, 0, 0.01f);
    for (int i = 0; i <= 32; i++) {
        float a = i * 6.2831853f / 32;
        glVertex3f(cosf(a)*0.95f, sinf(a)*0.95f, 0.01f);
    }
    glEnd();

    // Hour markers
    glColor4f(lerp(0.9f, 0.8f, bw), lerp(0.9f, 0.8f, bw), lerp(0.9f, 0.8f, bw), 1.0f);
    glLineWidth(2.0f);
    for (int i = 0; i < 12; i++) {
        float a = i * 6.2831853f / 12.0f;
        float r0 = (i % 3 == 0) ? 0.75f : 0.82f;
        glBegin(GL_LINES);
        glVertex3f(cosf(a)*r0, sinf(a)*r0, 0.02f);
        glVertex3f(cosf(a)*0.9f, sinf(a)*0.9f, 0.02f);
        glEnd();
    }

    // Hands
    auto hand = [&](float angleDeg, float len, float thick, float r, float g, float b) {
        glPushMatrix();
        glRotatef(-angleDeg + 90.0f, 0, 0, 1);
        glLineWidth(thick);
        glColor4f(lerp(r, r*0.5f, bw), lerp(g, g*0.5f, bw), lerp(b, b*0.5f, bw), 1.0f);
        glBegin(GL_LINES);
        glVertex3f(0, -0.15f, 0.03f);
        glVertex3f(0, len, 0.03f);
        glEnd();
        glPopMatrix();
    };
    hand(hours   * 30.0f,  0.50f, 4.0f, 0.9f, 0.85f, 0.7f);
    hand(minutes * 6.0f,   0.72f, 3.0f, 0.9f, 0.85f, 0.7f);
    hand(seconds * 6.0f,   0.80f, 1.5f, 0.9f, 0.15f, 0.1f);

    glPopMatrix();
}

// ─────────────────────────────────────────────────────────────────────────────
// SCENE STATE
// ─────────────────────────────────────────────────────────────────────────────

static Stickman leo;
static Ball     ball;
static float    flashTimer = 0;

void initScene(int s) {
    gTime = 0.0f;
    rainFrozen = false;

    switch (s) {
    case 0: // Title
        break;
    case 1: // Discovery
        initRain();
        leo = {};
        leo.px = -8.0f; leo.py = 0; leo.pz = -5.0f;
        leo.scale = 0.85f;
        leo.heading = 70.0f;
        leo.frozen = false;
        leo.computePose();
        break;
    case 2: // Watch Macro
        initGears();
        break;
    case 3: // Stadium
        ball.x = 0; ball.y = 1.0f; ball.z = -18.0f;
        ball.vx = 0.2f; ball.vy = 7.5f; ball.vz = 9.0f;
        ball.frozen = false;
        flashTimer = 0;
        // fielder (stickman far right)
        leo = {};
        leo.px = 6.0f; leo.py = 0; leo.pz = -4.0f;
        leo.scale = 0.85f; leo.heading = 180.0f;
        leo.frozen = false;
        leo.computePose();
        break;
    case 4: // Closed loop
        initRain();
        rainFrozen = false;
        leo = {};
        leo.px = -10.0f; leo.py = 0; leo.pz = -8.0f;
        leo.scale = 0.85f; leo.heading = 60.0f;
        leo.frozen = false;
        leo.computePose();
        break;
    case 5: // Berlin 1945
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  SCENE DRAW FUNCTIONS
// ─────────────────────────────────────────────────────────────────────────────

/* ── SCENE 0: TITLE ───────────────────────────────────────────────────────── */
void drawScene0() {
    float t = sceneStoryTime(0, gTime);
    float fade = smoothstep(clamp01(t / 1.2f));
    float subFade = smoothstep(clamp01((t - 1.2f) / 1.0f));
    float credFade = smoothstep(clamp01((t - 2.4f) / 1.4f));
    float frameFade = smoothstep(clamp01((t - 0.5f) / 0.8f));
    float cx = W * 0.5f;
    float cy = H * 0.56f;

    glClearColor(0.01f, 0.01f, 0.03f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    begin2D();

    // "24" logo – large glowing numerals
    glBegin(GL_QUADS);
    glColor4f(0.01f, 0.01f, 0.03f, 1.0f);
    glVertex2f(0, H);
    glVertex2f(W, H);
    glColor4f(0.05f, 0.03f, 0.02f, 1.0f);
    glVertex2f(W, 0);
    glVertex2f(0, 0);
    glEnd();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 0.0f, 0.0f, 0.45f * frameFade);
    glBegin(GL_QUADS);
    glVertex2f(0, 0);
    glVertex2f(W, 0);
    glVertex2f(W, 55);
    glVertex2f(0, 55);
    glVertex2f(0, H - 55);
    glVertex2f(W, H - 55);
    glVertex2f(W, H);
    glVertex2f(0, H);
    glEnd();

    float warmPulse = 0.75f + 0.25f * pulse(t, 0.18f);
    drawGlow(cx, cy + 6.0f, 150.0f, 1.0f, 0.40f, 0.08f, 0.42f * fade * warmPulse);
    drawGlow(cx, cy + 6.0f, 92.0f, 1.0f, 0.75f, 0.20f, 0.22f * fade);
    glColor4f(1.0f, 0.43f, 0.08f, 0.18f * fade);
    drawRing2D(cx, cy + 6.0f, 118.0f, 126.0f, 96);
    glColor4f(1.0f, 0.75f, 0.30f, 0.08f * fade);
    drawRing2D(cx, cy + 6.0f, 142.0f, 145.0f, 96);

    float sweep = t * 1.15f;
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    for (int i = 0; i < 60; i++) {
        float a = i * 6.2831853f / 60.0f - 1.5707963f;
        float inner = (i % 5 == 0) ? 132.0f : 137.0f;
        float outer = (i % 5 == 0) ? 150.0f : 145.0f;
        float sparkle = 0.25f + 0.75f * powf(std::max(0.0f, cosf(a - sweep)), 6.0f);
        glColor4f(1.0f, 0.68f, 0.24f, fade * (0.10f + 0.20f * sparkle));
        glVertex2f(cx + cosf(a) * inner, cy + 6.0f + sinf(a) * inner);
        glVertex2f(cx + cosf(a) * outer, cy + 6.0f + sinf(a) * outer);
    }
    glEnd();

    glColor4f(1.0f, 0.55f, 0.16f, 0.28f * fade);
    glLineWidth(3.0f);
    glBegin(GL_LINES);
    glVertex2f(cx, cy + 6.0f);
    glVertex2f(cx + cosf(sweep - 1.5707963f) * 102.0f, cy + 6.0f + sinf(sweep - 1.5707963f) * 102.0f);
    glEnd();

    std::string title = "24";
    float titleScale = 2.15f;
    float titleY = cy - 72.0f;
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    for (int layer = 6; layer >= 1; layer--) {
        float offset = layer * 1.8f;
        float layerScale = titleScale + layer * 0.025f;
        float a = fade * 0.055f * layer;
        drawStrokeTextCentered(cx + offset * 0.25f, titleY - offset * 0.45f, title, layerScale, 1.0f, 0.34f, 0.02f, a);
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    drawStrokeTextCentered(cx, titleY, title, titleScale, 1.0f, 0.76f, 0.16f, fade);
    drawStrokeTextCentered(cx, titleY, title, titleScale - 0.06f, 1.0f, 0.92f, 0.72f, fade * 0.30f);

    glColor4f(1.0f, 0.62f, 0.22f, 0.32f * frameFade);
    drawRing2D(cx, cy + 6.0f, 170.0f, 171.5f, 96);
    glBegin(GL_LINES);
    glVertex2f(cx - 215.0f, cy - 115.0f);
    glVertex2f(cx - 78.0f,  cy - 115.0f);
    glVertex2f(cx + 78.0f,  cy - 115.0f);
    glVertex2f(cx + 215.0f, cy - 115.0f);
    glEnd();

    drawTextCentered(cx, cy + 125.0f, "A TIME-BENDING FAN FILM", 0.72f, 0.74f, 0.78f, fade * 0.85f, GLUT_BITMAP_HELVETICA_12);
    drawTextCentered(cx, cy - 108.0f, "THE OPENGL CUT", 0.94f, 0.90f, 0.84f, subFade, GLUT_BITMAP_HELVETICA_18);
    drawTextCentered(cx, cy - 145.0f, "Written and Directed by", 0.66f, 0.68f, 0.72f, credFade * 0.92f, GLUT_BITMAP_HELVETICA_12);
    drawTextCentered(cx, cy - 174.0f, "Karthik and Rohith", 0.96f, 0.90f, 0.80f, credFade, GLUT_BITMAP_TIMES_ROMAN_24);
    drawTextCentered(cx, 70.0f, "Special Thanks to Ms. Alakananda K.", 0.76f, 0.76f, 0.78f, credFade * 0.95f, GLUT_BITMAP_HELVETICA_12);
    drawTextCentered(cx, 108.0f, "A strange book. A ticking watch. A night ready to break.", 0.78f, 0.80f, 0.84f, subFade * 0.75f, GLUT_BITMAP_HELVETICA_12);

    if (t < 0.28f) {
        float lf = 1.0f - t / 0.28f;
        glColor4f(1.0f, 1.0f, 1.0f, lf * 0.85f);
        glBegin(GL_QUADS);
        glVertex2f(0, 0);
        glVertex2f(W, 0);
        glVertex2f(W, H);
        glVertex2f(0, H);
        glEnd();
    }

    end2D();
}

/* ── SCENE 1: RAIN & DISCOVERY ────────────────────────────────────────────── */
void drawScene1() {
    float t = sceneStoryTime(1, gTime);
    // 0-4s: Leo walks, rain falls
    // 4-7s: Leo pulls crown → rain freezes (rainFrozen set in update)
    // 7-end: held frozen

    bool showMacroWatch = (t > 5.4f && t < 8.4f);
    bool rewindingRain = (t >= 8.4f && t < 10.8f);

    if (showMacroWatch) {
        drawMacroWatchShot("SCENE 1",
                           "THE DISCOVERY AND THE RAIN",
                           clamp01((t - 5.4f) / 3.0f),
                           "MACRO SHOT: Leo pulls the crown with a loud CLICK.",
                           "The crown comes out. The needles hesitate, then rewind.",
                           0.46f, 0.78f, 1.00f);
        return;
    }

    float wetness = 1.0f - smoothstep(clamp01((t - 8.4f) / 2.0f));

    // Night-blue sky
    glClearColor(0.03f, 0.05f, 0.10f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Camera
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(60, (float)W/H, 0.1f, 200.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(0, 5, 12, 0, 1, -5, 0, 1, 0);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    drawStreet(wetness);

    // Leo walking
    leo.draw(0.90f, 0.75f, 0.60f);

    // Book on ground (appears after t>3)
    if (t > 3.0f) {
        float bookAlpha = smoothstep(clamp01((t - 3.0f) / 0.5f));
        glColor4f(0.40f, 0.25f, 0.10f, bookAlpha);
        glPushMatrix();
        glTranslatef(-1.5f, 0.05f, -3.0f);
        glRotatef(30, 0, 1, 0);
        glScalef(0.4f, 0.06f, 0.28f);
        glutSolidCube(1.0f);
        glPopMatrix();
    }

    // Rain
    drawRain(1.0f, rewindingRain);

    // Freeze flash effect
    if (t > 6.5f && t < 7.2f) {
        float ff = 1.0f - (t - 6.5f) / 0.7f;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glColor4f(0.5f, 0.7f, 1.0f, ff * 0.4f);
        begin2D();
        glBegin(GL_QUADS);
        glVertex2f(0,0); glVertex2f(W,0); glVertex2f(W,H); glVertex2f(0,H);
        glEnd();
        end2D();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    begin2D();
    drawSceneBanner("SCENE 1", "THE DISCOVERY AND THE RAIN", smoothstep(clamp01(t / 0.8f)));

    float panelAlpha = smoothstep(clamp01((t - 4.0f) / 0.4f));
    if (panelAlpha > 0.01f) {
        glColor4f(0.04f, 0.05f, 0.08f, 0.78f * panelAlpha);
        glBegin(GL_QUADS);
        glVertex2f(W - 270, H - 110); glVertex2f(W - 26, H - 110);
        glVertex2f(W - 26,  H - 26);  glVertex2f(W - 270, H - 26);
        glEnd();
        glColor4f(0.95f, 0.82f, 0.46f, panelAlpha);
        glBegin(GL_QUADS);
        glVertex2f(W - 270, H - 34); glVertex2f(W - 26, H - 34);
        glVertex2f(W - 26,  H - 30); glVertex2f(W - 270, H - 30);
        glEnd();
        drawText(W - 248, H - 54, "WATCH STATUS", 0.96f, 0.90f, 0.78f, panelAlpha, GLUT_BITMAP_HELVETICA_12);
        drawText(W - 248, H - 76, (t < 6.5f) ? "Offset: -05 minutes" : "Rewind target: -30 minutes", 0.86f, 0.88f, 0.92f, panelAlpha, GLUT_BITMAP_HELVETICA_12);
        drawText(W - 248, H - 96, (t < 8.4f) ? "Rain: falling" : ((t < 10.8f) ? "Rain: reversing upward" : "Rain: frozen in air"), 0.56f, 0.80f, 1.00f, panelAlpha, GLUT_BITMAP_HELVETICA_12);
    }

    if (t < 3.8f) {
        float a = smoothstep(clamp01(t / 0.5f)) * smoothstep(clamp01((3.8f - t) / 0.5f));
        drawTextCentered(W / 2.0f, 64.0f, "Leo spots an ornate book on a rain-swept street.", 0.92f, 0.92f, 0.92f, a, GLUT_BITMAP_HELVETICA_12);
    }
    if (t > 3.2f && t < 6.8f) {
        float a = smoothstep(clamp01((t - 3.2f) / 0.5f)) * smoothstep(clamp01((6.8f - t) / 0.5f));
        drawTextCentered(W / 2.0f, 64.0f, "Inside it: a heavy mechanical watch, already ticking.", 1.00f, 0.90f, 0.56f, a, GLUT_BITMAP_HELVETICA_12);
    }
    if (t > 5.2f && t < 9.2f) {
        float a = smoothstep(clamp01((t - 5.2f) / 0.5f)) * smoothstep(clamp01((9.2f - t) / 0.5f));
        drawTextCentered(W / 2.0f, 64.0f, "He sets it five minutes behind. The city falls silent.", 0.76f, 0.88f, 1.00f, a, GLUT_BITMAP_HELVETICA_12);
    }
    if (t > 8.4f && t < 10.8f) {
        float a = smoothstep(clamp01((t - 8.4f) / 0.3f)) * smoothstep(clamp01((10.8f - t) / 0.5f));
        drawTextCentered(W / 2.0f, 40.0f, "The rain starts climbing back into the sky.", 0.54f, 0.84f, 1.00f, a, GLUT_BITMAP_HELVETICA_18);
    } else if (t > 10.8f) {
        float a = smoothstep(clamp01((t - 10.8f) / 0.4f));
        drawTextCentered(W / 2.0f, 40.0f, "He pulls the crown. Rain hangs motionless in mid-air.", 0.54f, 0.84f, 1.00f, a, GLUT_BITMAP_HELVETICA_18);
    }
    if (t > 5.0f && t < 8.4f) {
        float a = smoothstep(clamp01((t - 5.0f) / 0.4f)) * smoothstep(clamp01((8.4f - t) / 0.5f));
        drawTextCentered(W / 2.0f, 20.0f, "A macro close-up fills the frame: brass gears, crown teeth, hands reversing.", 0.88f, 0.88f, 0.94f, a, GLUT_BITMAP_HELVETICA_12);
    } else if (t > 9.0f) {
        float a = smoothstep(clamp01((t - 9.0f) / 0.4f));
        drawTextCentered(W / 2.0f, 20.0f, "Then he turns it toward thirty minutes, and the whole night starts to unwind.", 0.88f, 0.88f, 0.94f, a, GLUT_BITMAP_HELVETICA_12);
    }
    end2D();
}

/* ── SCENE 2: WATCH MACRO ─────────────────────────────────────────────────── */
void drawScene2() {
    float t = sceneStoryTime(2, gTime);

    glClearColor(0.04f, 0.03f, 0.02f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Orthographic close-up
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-3.5f, 3.5f, -2.0f, 2.0f, -5, 5);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Phase: rewind (t<5) or normal (t>=5)
    bool rewinding = (t > 2.0f && t < 6.5f);
    float timeWarp = rewinding ? -1.0f : 1.0f;
    float spinSpeed = rewinding ? 3.0f : 1.0f;

    // Background poster memory of the match that Leo will later rewrite.
    if (gAwakeningBgTex) {
        float pan = 0.02f * sinf(t * 0.35f);
        float zoom = 0.06f * smoothstep(clamp01((t - 1.0f) / 5.0f));
        drawTexturedQuad2D(gAwakeningBgTex,
                           -3.9f - zoom, -2.2f - zoom,
                            3.9f + zoom,  2.2f + zoom,
                            0.36f,
                            0.02f + pan, 0.02f,
                            0.98f + pan, 0.98f);

        glBegin(GL_QUADS);
        glColor4f(0.01f, 0.01f, 0.02f, 0.80f);
        glVertex3f(-3.7f,  2.0f, -0.8f);
        glVertex3f( 3.7f,  2.0f, -0.8f);
        glColor4f(0.14f, 0.09f, 0.04f, 0.56f);
        glVertex3f( 3.7f, -2.0f, -0.8f);
        glVertex3f(-3.7f, -2.0f, -0.8f);
        glEnd();
    }

    // Background glow
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.4f, 0.3f, 0.1f, 0.12f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(0, 0, -0.5f);
    for (int i = 0; i <= 32; i++) {
        float a = i * 6.2831853f / 32;
        glVertex3f(cosf(a)*3.5f, sinf(a)*3.5f, -0.5f);
    }
    glEnd();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (auto& ge : gears)
        drawGear(ge, 1.0f);

    // Watch bezel ring
    glColor4f(0.65f, 0.52f, 0.25f, 1.0f);
    glLineWidth(8.0f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 64; i++) {
        float a = i * 6.2831853f / 64;
        glVertex3f(cosf(a)*1.8f, sinf(a)*1.8f, 0.1f);
    }
    glEnd();

    // Crown  (right side)
    glColor4f(0.7f, 0.58f, 0.28f, 1.0f);
    float crownPull = (t > 1.5f && t < 7.0f) ? 0.25f : 0.0f;
    glPushMatrix();
    glTranslatef(1.8f + crownPull, 0, 0.1f);
    glScalef(0.35f, 0.18f, 1);
    glutSolidCube(1.0f);
    glPopMatrix();

    // Rotating hands on top
    glPushMatrix();
    glTranslatef(0, 0, 0.15f);
    float handAngle = t * (rewinding ? -120.0f : 40.0f);
    // Hour hand
    glPushMatrix();
    glRotatef(handAngle * 0.083f, 0, 0, 1);
    glColor4f(0.9f, 0.85f, 0.7f, 1.0f);
    glLineWidth(5.0f);
    glBegin(GL_LINES);
    glVertex3f(0, -0.2f, 0); glVertex3f(0, 0.7f, 0);
    glEnd();
    glPopMatrix();
    // Minute hand
    glPushMatrix();
    glRotatef(handAngle, 0, 0, 1);
    glColor4f(0.9f, 0.85f, 0.7f, 1.0f);
    glLineWidth(3.0f);
    glBegin(GL_LINES);
    glVertex3f(0, -0.2f, 0); glVertex3f(0, 1.1f, 0);
    glEnd();
    glPopMatrix();
    // Second hand
    glPushMatrix();
    glRotatef(handAngle * 12.0f, 0, 0, 1);
    glColor4f(0.9f, 0.2f, 0.1f, 1.0f);
    glLineWidth(1.5f);
    glBegin(GL_LINES);
    glVertex3f(0, -0.3f, 0); glVertex3f(0, 1.3f, 0);
    glEnd();
    glPopMatrix();

    // Centre jewel
    glColor4f(0.1f, 0.4f, 0.9f, 1.0f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(0, 0, 0.01f);
    for (int i = 0; i <= 16; i++) {
        float a = i * 6.2831853f / 16;
        glVertex3f(cosf(a)*0.08f, sinf(a)*0.08f, 0);
    }
    glEnd();
    drawGlow(0, 0, 0.3f, 0.2f, 0.5f, 1.0f, rewinding ? 1.0f : 0.5f);

    glPopMatrix();

    // Rewind flash
    if (rewinding) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glColor4f(0.2f, 0.5f, 1.0f, 0.05f * sinf(t * 20));
        glBegin(GL_QUADS);
        glVertex2f(-4, -3); glVertex2f(4, -3); glVertex2f(4, 3); glVertex2f(-4, 3);
        glEnd();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    begin2D();
    drawSceneBanner("SCENE 2", "THE AWAKENING", smoothstep(clamp01(t / 0.8f)));

    float logoAlpha = smoothstep(clamp01((t - 0.2f) / 0.6f)) * (1.0f - smoothstep(clamp01((t - 4.2f) / 1.0f)));
    if (logoAlpha > 0.01f) {
        drawGlow(W * 0.5f, H * 0.56f, 120.0f, 1.0f, 0.46f, 0.08f, logoAlpha * 0.36f);
        drawStrokeTextCentered(W * 0.5f, H * 0.56f - 56.0f, "24", 2.05f, 1.0f, 0.78f, 0.18f, logoAlpha);
        drawTextCentered(W * 0.5f, H * 0.56f - 102.0f, "THE OPENGL CUT", 0.96f, 0.90f, 0.84f, logoAlpha, GLUT_BITMAP_HELVETICA_18);
    }

    float creditAlpha = smoothstep(clamp01((t - 3.0f) / 0.8f)) * (1.0f - smoothstep(clamp01((t - 7.8f) / 1.0f)));
    if (creditAlpha > 0.01f) {
        drawTextCentered(W * 0.5f, 82.0f, "Written and Directed by Karthik and Rohith", 0.76f, 0.76f, 0.78f, creditAlpha, GLUT_BITMAP_HELVETICA_12);
    }

    if (t < 2.8f) {
        float a = smoothstep(clamp01(t / 0.5f)) * smoothstep(clamp01((2.8f - t) / 0.5f));
        drawTextCentered(W * 0.5f, 52.0f, "Lightning tears across the dark. The logo wakes first.", 0.94f, 0.90f, 0.82f, a, GLUT_BITMAP_HELVETICA_12);
    } else if (t < 6.5f) {
        float a = smoothstep(clamp01((t - 2.8f) / 0.5f)) * smoothstep(clamp01((6.5f - t) / 0.5f));
        drawTextCentered(W * 0.5f, 52.0f, "The glowing 24 breaks apart into brass, glass and ticking gears.", 0.98f, 0.88f, 0.60f, a, GLUT_BITMAP_HELVETICA_12);
    } else {
        float a = smoothstep(clamp01((t - 6.5f) / 0.5f));
        drawTextCentered(W * 0.5f, 52.0f, rewinding ? "The mainspring tightens. Time drags itself backward." : "Tick...  tick...  tick...", 0.80f, 0.75f, 0.50f, a, GLUT_BITMAP_HELVETICA_18);
    }
    end2D();
}

/* ── SCENE 3: STADIUM ─────────────────────────────────────────────────────── */
void drawScene3() {
    float t = sceneStoryTime(3, gTime);
    bool showMacroWatch = (t > 4.0f && t < 6.2f);
    bool rewindingBall = (t >= 6.2f && t < 8.6f);
    bool frozen  = (t >= 8.6f && t < 13.0f);
    bool resumed = (t >= 13.0f);

    if (showMacroWatch) {
        drawMacroWatchShot("SCENE 3",
                           "THE KING'S RESCUE",
                           clamp01((t - 4.0f) / 2.2f),
                           "MACRO SHOT: Leo yanks the crown. The mainspring tightens.",
                           "Needles reverse first. Then the stadium starts to rewind.",
                           1.00f, 0.84f, 0.34f);
        return;
    }
    flashTimer += 0.016f;

    glClearColor(0.04f, 0.04f, 0.06f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(55, (float)W/H, 0.1f, 200.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Camera slowly pans during frozen section
    if (rewindingBall) {
        gluLookAt(0, 5, 10, ball.x, ball.y, ball.z, 0, 1, 0);
    } else {
        float camX = frozen ? lerp(0, 4, clamp01((t - 8.6f) / 3.0f)) : 0;
        gluLookAt(camX, 6, 16, 0, 2, -5, 0, 1, 0);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float flashA = (frozen || rewindingBall) ? 0 : 0.5f + 0.5f * sinf(flashTimer * 8.0f);
    drawStadium(flashA * 0.3f);

    // Fielder (stickman)
    float fielderColor = (frozen || rewindingBall) ? 0.5f : 1.0f;
    leo.draw(0.85f * fielderColor, 0.85f * fielderColor, 1.0f * fielderColor);

    // Ball
    ball.draw();

    // Leo (our hero) on the field during frozen section
    if (t > 8.9f && t < 13.5f) {
        float walkFade = smoothstep(clamp01((t - 8.9f) / 0.8f));
        Stickman hero;
        hero = {};
        float hx = lerp(-9, 5.5f, clamp01((t - 9.1f) / 2.8f));
        hero.px = hx; hero.py = 0; hero.pz = -3.5f;
        hero.scale = 0.85f; hero.heading = 90.0f;
        hero.frozen = false; hero.walkPhase = t * 2.0f;
        hero.computePose();
        hero.draw(0.95f, 0.85f, 0.6f, walkFade);
    }

    // Depth of field fake: blur overlay for far stands
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.04f, 0.04f, 0.06f, 0.45f);
    glPushMatrix();
    glTranslatef(0, 0, -18);
    glBegin(GL_QUADS);
    glVertex3f(-25, 0, 0); glVertex3f(25, 0, 0);
    glVertex3f(25, 15, 0); glVertex3f(-25, 15, 0);
    glEnd();
    glPopMatrix();

    // "SIX!" text
    if (resumed) {
        float sixAlpha = smoothstep(clamp01((t - 13.5f) / 0.5f));
        drawGlow(0, 8, 3.0f, 1.0f, 0.9f, 0.0f, sixAlpha);
        begin2D();
        glColor4f(1.0f, 0.95f, 0.1f, sixAlpha);
        std::string s = "SIX!!!";
        glRasterPos2f(W/2.0f - s.size()*9, H/2.0f + 60);
        for (char c : s) glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, c);
        end2D();
    }

    // Scoreboard overlay
    begin2D();
    float sbAlpha = 0.9f;
    drawSceneBanner("SCENE 3", "THE KING'S RESCUE", smoothstep(clamp01(t / 0.8f)));
    // Board background
    glColor4f(0.05f, 0.05f, 0.05f, sbAlpha);
    glBegin(GL_QUADS);
    glVertex2f(W-220, H-90); glVertex2f(W-10, H-90);
    glVertex2f(W-10, H-10);  glVertex2f(W-220, H-10);
    glEnd();
    glColor4f(1.0f, 0.9f, 0.2f, sbAlpha);
    drawText(W-210, H-40, "CSK  vs  RCB", 1, 0.9f, 0.2f, sbAlpha);
    drawText(W-210, H-65, "Need 6 off 1 ball", 0.9f, 0.9f, 0.9f, sbAlpha);
    if (frozen)
        drawText(W-210, H-82, "TIME FROZEN", 0.3f, 0.7f, 1.0f, sbAlpha);
    else if (rewindingBall)
        drawText(W-210, H-82, "BALL REWIND", 1.0f, 0.75f, 0.25f, sbAlpha);
    else if (resumed)
        drawText(W-210, H-82, "CSK WIN!!!  +6", 0.2f, 1.0f, 0.2f, sbAlpha);

    if (t < 3.8f) {
        float a = smoothstep(clamp01(t / 0.5f)) * smoothstep(clamp01((3.8f - t) / 0.5f));
        drawTextCentered(W / 2.0f, 64.0f, "One ball left. Six runs needed. The shot climbs into the stadium lights.", 0.96f, 0.92f, 0.84f, a, GLUT_BITMAP_HELVETICA_12);
    }
    if (t > 3.0f && t < 5.3f) {
        float a = smoothstep(clamp01((t - 3.0f) / 0.4f)) * smoothstep(clamp01((5.3f - t) / 0.4f));
        drawTextCentered(W / 2.0f, 44.0f, "The boundary fielder takes it clean. Leo refuses the ending.", 0.96f, 0.74f, 0.56f, a, GLUT_BITMAP_HELVETICA_12);
    }
    if (t > 4.0f && t < 6.2f) {
        float a = smoothstep(clamp01((t - 4.0f) / 0.4f)) * smoothstep(clamp01((6.2f - t) / 0.5f));
        drawTextCentered(W / 2.0f, 64.0f, "A macro shot takes over: crown out, spring tight, needles reversing.", 0.48f, 0.82f, 1.00f, a, GLUT_BITMAP_HELVETICA_18);
    }
    if (t > 6.2f && t < 8.6f) {
        float a = smoothstep(clamp01((t - 6.2f) / 0.4f)) * smoothstep(clamp01((8.6f - t) / 0.5f));
        drawTextCentered(W / 2.0f, 44.0f, "The catch rewinds. The ball backs away from the fielder and rises again.", 0.98f, 0.82f, 0.42f, a, GLUT_BITMAP_HELVETICA_12);
    }
    if (t > 8.8f && t < 12.4f) {
        float a = smoothstep(clamp01((t - 8.8f) / 0.5f)) * smoothstep(clamp01((12.4f - t) / 0.5f));
        drawTextCentered(W / 2.0f, 44.0f, "Leo strolls through the frozen field and nudges the falling ball two feet left.", 0.92f, 0.86f, 0.62f, a, GLUT_BITMAP_HELVETICA_12);
    }
    if (resumed) {
        float a = smoothstep(clamp01((t - 13.0f) / 0.5f));
        drawTextCentered(W / 2.0f, 24.0f, "He sits back down. The catch becomes a six.", 0.86f, 1.00f, 0.72f, a, GLUT_BITMAP_HELVETICA_18);
    }
    end2D();
}

/* ── SCENE 4: CLOSED LOOP ─────────────────────────────────────────────────── */
void drawScene4() {
    float t = sceneStoryTime(4, gTime);

    glClearColor(0.03f, 0.05f, 0.10f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(60, (float)W/H, 0.1f, 200.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(0, 5, 12, 0, 1, -5, 0, 1, 0);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    drawStreet(0.9f);
    drawRain(0.8f);

    // Past-Leo (transparent) placing book
    if (t > 3.0f) {
        float pLeoAlpha = smoothstep(clamp01((t - 3.0f) / 1.0f));
        Stickman pastLeo;
        pastLeo = {};
        pastLeo.px = -2.0f; pastLeo.py = 0; pastLeo.pz = -4.0f;
        pastLeo.scale = 0.85f; pastLeo.heading = -30.0f;
        pastLeo.frozen = false; pastLeo.walkPhase = t * 1.5f;
        pastLeo.computePose();
        pastLeo.draw(0.4f, 0.8f, 1.0f, pLeoAlpha * 0.65f); // ghostly blue
    }

    // Present Leo sneaking
    leo.draw(0.90f, 0.75f, 0.60f);

    // Book being placed
    if (t > 5.0f) {
        float bA = smoothstep(clamp01((t - 5.0f) / 0.6f));
        glColor4f(0.40f, 0.25f, 0.10f, bA);
        glPushMatrix();
        glTranslatef(-1.5f, 0.05f, -3.0f);
        glRotatef(30, 0, 1, 0);
        glScalef(0.4f, 0.06f, 0.28f);
        glutSolidCube(1.0f);
        glPopMatrix();
    }

    // Blue time-portal effect
    if (t > 1.0f && t < 4.0f) {
        float portalAlpha = smoothstep(clamp01((t-1.0f)/1.0f)) * smoothstep(clamp01((4.0f-t)/1.0f));
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glColor4f(0.1f, 0.3f, 1.0f, portalAlpha * 0.3f);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(-2, 2, -5);
        for (int i = 0; i <= 32; i++) {
            float a = i * 6.2831853f / 32;
            glVertex3f(-2 + cosf(a)*1.5f, 2 + sinf(a)*2.2f, -5);
        }
        glEnd();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    // Subtitles
    begin2D();
    drawSceneBanner("SCENE 4", "THE CLOSED LOOP", smoothstep(clamp01(t / 0.8f)));
    if (t < 3.5f) {
        float a = smoothstep(clamp01(t / 0.5f)) * smoothstep(clamp01((3.5f - t) / 0.5f));
        drawTextCentered(W / 2.0f, 64.0f, "Leo understands the truth: the watch was never random. He was always part of it.", 1.0f, 1.0f, 0.76f, a, GLUT_BITMAP_HELVETICA_12);
    }
    if (t > 1.2f && t < 4.5f) {
        float a = smoothstep(clamp01((t - 1.2f) / 0.4f)) * smoothstep(clamp01((4.5f - t) / 0.5f));
        drawTextCentered(W / 2.0f, 42.0f, "He drives the hands back to the exact night he first found it.", 0.58f, 0.90f, 1.00f, a, GLUT_BITMAP_HELVETICA_12);
    }
    if (t > 4.0f && t < 9.0f) {
        float a = smoothstep(clamp01((t - 4.0f) / 0.5f)) * smoothstep(clamp01((9.0f - t) / 0.5f));
        drawTextCentered(W / 2.0f, 64.0f, "From a new angle, he slips into the alley and leaves the book for his past self.", 0.5f, 0.9f, 1.0f, a, GLUT_BITMAP_HELVETICA_12);
    }
    if (t > 9.0f) {
        float a = smoothstep(clamp01((t - 9.0f) / 0.5f));
        drawTextCentered(W / 2.0f, H / 2.0f, "Destiny fulfilled.", 0.84f, 0.84f, 0.84f, a, GLUT_BITMAP_TIMES_ROMAN_24);
    }
    end2D();
}

/* ── SCENE 5: BERLIN 1945 (B&W) ──────────────────────────────────────────── */
void drawBunkerDictatorFigure(float alpha = 1.0f) {
    auto grey = [&](float v) { glColor4f(v, v, v, alpha); };
    auto line = [&](float x0, float y0, float z0, float x1, float y1, float z1, float width) {
        glLineWidth(width);
        glBegin(GL_LINES);
        glVertex3f(x0, y0, z0);
        glVertex3f(x1, y1, z1);
        glEnd();
    };

    grey(0.11f);
    glBegin(GL_QUADS);
    glVertex3f(-0.85f, 1.15f, -0.35f); glVertex3f(0.85f, 1.15f, -0.35f);
    glVertex3f(0.85f, 3.00f, -0.35f);  glVertex3f(-0.85f, 3.00f, -0.35f);
    glVertex3f(-0.65f, 0.75f, -0.15f); glVertex3f(0.65f, 0.75f, -0.15f);
    glVertex3f(0.65f, 1.15f, -0.15f);  glVertex3f(-0.65f, 1.15f, -0.15f);
    glEnd();

    grey(0.18f);
    glBegin(GL_POLYGON);
    glVertex3f(-0.72f, 2.70f, 0.00f);
    glVertex3f( 0.72f, 2.70f, 0.00f);
    glVertex3f( 0.56f, 1.15f, 0.05f);
    glVertex3f(-0.56f, 1.15f, 0.05f);
    glEnd();
    grey(0.14f);
    glBegin(GL_TRIANGLES);
    glVertex3f(-0.20f, 2.58f, 0.08f);
    glVertex3f( 0.00f, 1.35f, 0.08f);
    glVertex3f(-0.02f, 2.10f, 0.08f);
    glVertex3f( 0.20f, 2.58f, 0.08f);
    glVertex3f( 0.00f, 1.35f, 0.08f);
    glVertex3f( 0.02f, 2.10f, 0.08f);
    glEnd();

    grey(0.16f);
    Joint lArm0 = {-0.62f, 2.40f, 0.02f};
    Joint lArm1 = {-0.95f, 1.85f, 0.18f};
    Joint lArm2 = {-0.72f, 1.48f, 0.30f};
    Joint rArm0 = { 0.62f, 2.40f, 0.02f};
    Joint rArm1 = { 0.95f, 1.85f, 0.18f};
    Joint rArm2 = { 0.72f, 1.48f, 0.30f};
    drawRibbonSegment(lArm0, lArm1, 0.10f);
    drawRibbonSegment(lArm1, lArm2, 0.09f);
    drawRibbonSegment(rArm0, rArm1, 0.10f);
    drawRibbonSegment(rArm1, rArm2, 0.09f);

    grey(0.34f);
    drawDiskXY(-0.72f, 1.44f, 0.34f, 0.11f, 18);
    drawDiskXY( 0.72f, 1.44f, 0.34f, 0.11f, 18);

    grey(0.30f);
    glBegin(GL_QUADS);
    glVertex3f(-0.08f, 2.95f, 0.08f); glVertex3f(0.08f, 2.95f, 0.08f);
    glVertex3f(0.07f, 2.73f, 0.08f);  glVertex3f(-0.07f, 2.73f, 0.08f);
    glEnd();

    grey(0.38f);
    drawDiskXY(0.0f, 3.28f, 0.12f, 0.40f, 32);
    grey(0.24f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(0.0f, 3.22f, 0.14f);
    for (int i = 0; i <= 16; i++) {
        float a = 3.1415926f + i * 3.1415926f / 16.0f;
        glVertex3f(cosf(a) * 0.36f, 3.24f + sinf(a) * 0.32f, 0.14f);
    }
    glEnd();

    grey(0.08f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(0.0f, 3.42f, 0.16f);
    for (int i = 0; i <= 20; i++) {
        float a = 0.20f + i * 2.74f / 20.0f;
        glVertex3f(cosf(a) * 0.38f, 3.33f + sinf(a) * 0.31f, 0.16f);
    }
    glEnd();
    glBegin(GL_QUADS);
    glVertex3f(-0.33f, 3.28f, 0.15f); glVertex3f(-0.24f, 3.28f, 0.15f);
    glVertex3f(-0.22f, 3.02f, 0.15f); glVertex3f(-0.31f, 3.02f, 0.15f);
    glVertex3f(0.24f, 3.28f, 0.15f);  glVertex3f(0.33f, 3.28f, 0.15f);
    glVertex3f(0.31f, 3.02f, 0.15f);  glVertex3f(0.22f, 3.02f, 0.15f);
    glEnd();

    grey(0.31f);
    drawDiskXY(-0.38f, 3.24f, 0.10f, 0.05f, 14);
    drawDiskXY( 0.38f, 3.24f, 0.10f, 0.05f, 14);

    grey(0.06f);
    line(-0.18f, 3.34f, 0.17f, -0.05f, 3.31f, 0.17f, 2.0f);
    line( 0.05f, 3.31f, 0.17f,  0.18f, 3.34f, 0.17f, 2.0f);
    line(-0.14f, 3.17f, 0.18f, -0.04f, 3.17f, 0.18f, 2.5f);
    line( 0.04f, 3.17f, 0.18f,  0.14f, 3.17f, 0.18f, 2.5f);
    line( 0.02f, 3.19f, 0.18f,  0.05f, 2.98f, 0.18f, 2.0f);
    line(-0.07f, 2.90f, 0.19f,  0.06f, 2.88f, 0.19f, 2.0f);

    grey(0.03f);
    glBegin(GL_QUADS);
    glVertex3f(-0.18f, 3.03f, 0.23f); glVertex3f(0.18f, 3.03f, 0.23f);
    glVertex3f(0.17f, 2.87f, 0.23f);  glVertex3f(-0.17f, 2.87f, 0.23f);

    glVertex3f(-0.24f, 3.00f, 0.22f); glVertex3f(-0.12f, 3.00f, 0.22f);
    glVertex3f(-0.08f, 2.88f, 0.22f); glVertex3f(-0.20f, 2.88f, 0.22f);

    glVertex3f(0.12f, 3.00f, 0.22f);  glVertex3f(0.24f, 3.00f, 0.22f);
    glVertex3f(0.20f, 2.88f, 0.22f);  glVertex3f(0.08f, 2.88f, 0.22f);
    glEnd();
    grey(0.08f);
    line(-0.18f, 2.95f, 0.24f, 0.18f, 2.95f, 0.24f, 2.6f);
    grey(0.10f);
    line(-0.12f, 2.95f, 0.22f, 0.12f, 2.95f, 0.22f, 1.5f);
    line(-0.04f, 3.00f, 0.22f, -0.12f, 2.92f, 0.22f, 1.2f);
    line( 0.04f, 3.00f, 0.22f,  0.12f, 2.92f, 0.22f, 1.2f);

    grey(0.04f);
    drawCircleOutlineXY(0.0f, 3.28f, 0.18f, 0.40f, 32);
    line(-0.72f, 2.70f, 0.10f, 0.72f, 2.70f, 0.10f, 2.0f);
    line(-0.56f, 1.15f, 0.10f, 0.56f, 1.15f, 0.10f, 2.0f);
}

void drawScene5() {
    const float introHold = 10.0f;
    const float flagHold  = 5.0f;
    const float bunkerStart = introHold + flagHold;
    float t = gTime;

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (t < introHold) {
        begin2D();
        float alpha = smoothstep(clamp01(t / 1.4f)) * smoothstep(clamp01((introHold - t) / 1.5f));
        drawTextCentered(W * 0.5f, H * 0.54f, "POST CREDIT SCENE:BERLIN 1945", 0.92f, 0.92f, 0.92f, alpha, GLUT_BITMAP_TIMES_ROMAN_24);
        end2D();
        return;
    }

    if (t < bunkerStart) {
        float u = clamp01((t - introHold) / flagHold);
        float flagAlpha = sinf(u * 3.1415926f);
        float zoomPadX = lerp(48.0f, 0.0f, smoothstep(u));
        float zoomPadY = lerp(28.0f, 0.0f, smoothstep(u));

        begin2D();
        glColor4f(0, 0, 0, 1);
        glBegin(GL_QUADS);
        glVertex2f(0, 0); glVertex2f((float)W, 0);
        glVertex2f((float)W, (float)H); glVertex2f(0, (float)H);
        glEnd();

        drawTexturedQuad2D(gBerlinFlagTex,
                           -zoomPadX, -zoomPadY,
                           W + zoomPadX, H + zoomPadY,
                           flagAlpha * 0.95f);

        glBegin(GL_QUADS);
        glColor4f(0.0f, 0.0f, 0.0f, 0.38f + 0.18f * (1.0f - flagAlpha));
        glVertex2f(0, 0); glVertex2f((float)W, 0);
        glVertex2f((float)W, (float)H); glVertex2f(0, (float)H);
        glEnd();

        drawTextCentered(W * 0.5f, 58.0f, "Berlin 1945", 0.88f, 0.88f, 0.88f, flagAlpha * 0.76f, GLUT_BITMAP_HELVETICA_18);
        end2D();
        return;
    }

    float bunkerT = t - bunkerStart;
    float revealAlpha = smoothstep(clamp01(bunkerT / 1.4f));
    float shotAlpha = smoothstep(clamp01((bunkerT - 9.4f) / 0.15f));
    float shotFlash = smoothstep(clamp01((bunkerT - 9.3f) / 0.08f)) * (1.0f - smoothstep(clamp01((bunkerT - 9.7f) / 0.18f)));
    float bloodAlpha = smoothstep(clamp01((bunkerT - 9.55f) / 0.20f));
    float slump = smoothstep(clamp01((bunkerT - 9.9f) / 1.8f));
    float leoFade = smoothstep(clamp01((bunkerT - 1.9f) / 1.1f));
    float leoX = lerp(-7.0f, -2.25f, clamp01((bunkerT - 1.9f) / 3.2f));
    float leoShotX = -1.45f;
    float leoShotY = 2.45f;
    float leoShotZ = -1.72f;
    float targetX = 0.18f;
    float targetY = 3.08f;
    float targetZ = -4.02f;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(50, (float)W/H, 0.1f, 200.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(0, 3, 10, 0, 2, 0, 0, 1, 0);

    glEnable(GL_DEPTH_TEST);

    auto grey = [&](float v, float a = 1.0f) { glColor4f(v, v, v, revealAlpha * a); };
    auto drawBloodSplash = [&](float cx, float cy, float cz, float alpha, float scale) {
        glColor4f(0.84f, 0.03f, 0.02f, alpha);
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(cx, cy, cz);
        for (int i = 0; i <= 18; ++i) {
            float ang = i * 6.2831853f / 18.0f;
            float rad = scale * (0.40f + 0.22f * sinf(ang * 5.0f + bunkerT * 8.0f));
            glVertex3f(cx + cosf(ang) * rad, cy + sinf(ang) * rad, cz);
        }
        glEnd();
        glBegin(GL_TRIANGLES);
        glVertex3f(cx + 0.05f, cy - 0.02f, cz + 0.02f);
        glVertex3f(cx + 0.38f, cy - 0.24f, cz + 0.01f);
        glVertex3f(cx + 0.20f, cy - 0.54f, cz + 0.01f);
        glVertex3f(cx - 0.03f, cy - 0.10f, cz + 0.01f);
        glVertex3f(cx - 0.28f, cy - 0.36f, cz + 0.01f);
        glVertex3f(cx - 0.10f, cy - 0.58f, cz + 0.01f);
        glEnd();
    };

    // Floor
    grey(0.15f);
    glBegin(GL_QUADS);
    glVertex3f(-8,0,-12); glVertex3f(8,0,-12);
    glVertex3f(8,0,5);    glVertex3f(-8,0,5);
    glEnd();
    // Walls
    grey(0.10f);
    glBegin(GL_QUADS);
    glVertex3f(-8,0,-12); glVertex3f(-8,0,5);
    glVertex3f(-8,6,5);   glVertex3f(-8,6,-12);
    glEnd();
    glBegin(GL_QUADS);
    glVertex3f(8,0,-12); glVertex3f(8,0,5);
    glVertex3f(8,6,5);   glVertex3f(8,6,-12);
    glEnd();
    grey(0.08f);
    glBegin(GL_QUADS);
    glVertex3f(-8,0,-12); glVertex3f(8,0,-12);
    glVertex3f(8,6,-12);  glVertex3f(-8,6,-12);
    glEnd();
    grey(0.06f);
    glBegin(GL_QUADS);
    glVertex3f(-8,6,-12); glVertex3f(8,6,-12);
    glVertex3f(8,6,5);    glVertex3f(-8,6,5);
    glEnd();

    grey(0.10f, 0.70f);
    glBegin(GL_QUADS);
    glVertex3f(-2,2,-11.92f); glVertex3f(2,2,-11.92f);
    glVertex3f(2,5,-11.92f);  glVertex3f(-2,5,-11.92f);
    glEnd();

    // Desk
    grey(0.20f);
    glPushMatrix();
    glTranslatef(0, 0.8f, -4.0f);
    glScalef(2.2f, 0.1f, 1.0f);
    glutSolidCube(1.0f);
    glPopMatrix();
    auto deskLeg = [&](float x, float z) {
        grey(0.18f);
        glPushMatrix();
        glTranslatef(x, 0.4f, z);
        glScalef(0.1f, 0.8f, 0.1f);
        glutSolidCube(1.0f);
        glPopMatrix();
    };
    deskLeg(-1.0f, -3.6f); deskLeg(1.0f, -3.6f);
    deskLeg(-1.0f, -4.4f); deskLeg(1.0f, -4.4f);

    grey(0.50f);
    glPushMatrix();
    glTranslatef(-0.65f, 0.87f, -3.78f);
    glRotatef(-8.0f, 0, 1, 0);
    glScalef(0.70f, 0.03f, 0.55f);
    glutSolidCube(1.0f);
    glPopMatrix();
    grey(0.42f);
    glPushMatrix();
    glTranslatef(0.58f, 0.88f, -4.05f);
    glRotatef(12.0f, 0, 1, 0);
    glScalef(0.48f, 0.03f, 0.38f);
    glutSolidCube(1.0f);
    glPopMatrix();

    grey(0.72f);
    glPushMatrix();
    glTranslatef(0.15f, 0.89f, -3.58f);
    glRotatef(90.0f, 1, 0, 0);
    drawDiskXY(0.0f, 0.0f, 0.0f, 0.11f, 24);
    grey(0.14f);
    drawCircleOutlineXY(0.0f, 0.0f, 0.01f, 0.09f, 24);
    glPopMatrix();

    // Dictator figure with impact beat.
    glPushMatrix();
    glTranslatef(0.12f + slump * 0.35f, 0.60f - slump * 0.26f, -4.45f + slump * 0.18f);
    glRotatef(-12.0f * slump, 0, 0, 1);
    glRotatef(6.0f * slump, 1, 0, 0);
    glScalef(0.78f, 0.78f, 0.78f);
    drawBunkerDictatorFigure(1.0f - 0.18f * slump);
    if (bloodAlpha > 0.01f) {
        drawBloodSplash(0.08f, 3.06f, 0.28f, bloodAlpha * (0.76f + 0.24f * shotFlash), 0.46f + 0.12f * shotAlpha);
    }
    glPopMatrix();

    // Desk-side lamp silhouette
    grey(0.16f);
    glPushMatrix();
    glTranslatef(-1.55f, 0.88f, -3.85f);
    glScalef(0.10f, 0.45f, 0.10f);
    glutSolidCube(1.0f);
    glPopMatrix();
    glBegin(GL_TRIANGLES);
    glVertex3f(-1.85f, 1.35f, -3.95f);
    glVertex3f(-1.25f, 1.35f, -3.95f);
    glVertex3f(-1.55f, 1.72f, -3.75f);
    glEnd();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.92f, 0.92f, 0.92f, 0.08f * revealAlpha);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(0, 6, -4);
    for (int i = 0; i <= 24; i++) {
        float a = i * 6.2831853f / 24;
        glVertex3f(cosf(a) * 3.0f, 0, -4 + sinf(a) * 3.0f);
    }
    glEnd();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (bunkerT > 1.9f) {
        Stickman leoB = {};
        leoB.px = (bunkerT < 9.3f) ? leoX : -2.25f;
        leoB.py = 0.0f;
        leoB.pz = -2.0f;
        leoB.scale = 0.85f;
        leoB.heading = 90.0f;
        leoB.frozen = (bunkerT > 5.4f);
        leoB.walkPhase = bunkerT * 1.4f;
        leoB.computePose();
        leoB.draw(0.75f, 0.75f, 0.75f, leoFade);
    }

    if (shotFlash > 0.01f) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glColor4f(1.0f, 0.82f, 0.54f, shotFlash * 0.95f);
        glLineWidth(4.0f);
        glBegin(GL_LINES);
        glVertex3f(leoShotX, leoShotY, leoShotZ);
        glVertex3f(targetX, targetY, targetZ);
        glEnd();
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(leoShotX, leoShotY, leoShotZ);
        for (int i = 0; i <= 8; ++i) {
            float a = i * 6.2831853f / 8.0f;
            glVertex3f(leoShotX + cosf(a) * 0.28f, leoShotY + sinf(a) * 0.18f, leoShotZ);
        }
        glEnd();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    if (bunkerT < 1.0f) {
        float ff = 1.0f - bunkerT;
        begin2D();
        glColor4f(0.9f, 0.9f, 0.9f, ff * 0.45f);
        glBegin(GL_QUADS);
        glVertex2f(0,0); glVertex2f(W,0); glVertex2f(W,H); glVertex2f(0,H);
        glEnd();
        end2D();
    }

    begin2D();
    glColor4f(0, 0, 0, 1.0f);
    glBegin(GL_QUADS);
    glVertex2f(0,0); glVertex2f(W,0); glVertex2f(W,60); glVertex2f(0,60);
    glEnd();
    glBegin(GL_QUADS);
    glVertex2f(0,H-60); glVertex2f(W,H-60); glVertex2f(W,H); glVertex2f(0,H);
    glEnd();

    drawSceneBanner("SCENE 5", "BERLIN, 1945", revealAlpha);
    if (bunkerT < 3.6f) {
        float a = smoothstep(clamp01(bunkerT / 0.6f)) * smoothstep(clamp01((3.6f - bunkerT) / 0.6f));
        drawTextCentered(W/2.0f, 44.0f, "The bunker breathes in black and white.", 0.9f, 0.9f, 0.9f, a, GLUT_BITMAP_HELVETICA_12);
    }
    if (bunkerT > 2.2f && bunkerT < 8.8f) {
        float a = smoothstep(clamp01((bunkerT - 2.2f) / 0.6f)) * smoothstep(clamp01((8.8f - bunkerT) / 0.6f));
        drawTextCentered(W/2.0f, 26.0f, "Leo steps out of the freeze and forces the ending himself.", 0.82f, 0.82f, 0.82f, a, GLUT_BITMAP_HELVETICA_12);
    }
    if (bunkerT > 9.2f && bunkerT < 13.8f) {
        float a = smoothstep(clamp01((bunkerT - 9.2f) / 0.25f)) * smoothstep(clamp01((13.8f - bunkerT) / 0.8f));
        drawTextCentered(W/2.0f, 30.0f, "One shot cracks the silence. Red spills into a black-and-white room.", 0.96f, 0.80f, 0.80f, a, GLUT_BITMAP_HELVETICA_12);
    }

    float credAlpha = smoothstep(clamp01((bunkerT - 15.8f) / 1.2f));
    if (credAlpha > 0.01f) {
        drawTextCentered(W/2.0f, H/2.0f + 40.0f, "Special Thanks to Ms. Alakananda K.", 0.8f, 0.8f, 0.8f, credAlpha, GLUT_BITMAP_HELVETICA_18);
        drawTextCentered(W/2.0f, H/2.0f + 10.0f, "Karthik  &  Rohith", 0.9f, 0.9f, 0.9f, credAlpha, GLUT_BITMAP_TIMES_ROMAN_24);
        drawTextCentered(W/2.0f, H/2.0f - 20.0f, "24 : THE OPENGL CUT", 1.0f, 1.0f, 1.0f, credAlpha, GLUT_BITMAP_HELVETICA_18);
    }
    end2D();
}

// ─────────────────────────────────────────────────────────────────────────────
//  SCENE UPDATE LOGIC
// ─────────────────────────────────────────────────────────────────────────────

void updateScene(float dt) {
    float t = currentSceneStoryTime();
    float sdt = currentSceneStoryDt(dt);

    switch (gScene) {
    case 1:
        if (t < 5.0f) {
            leo.px += sdt * 0.6f;
            leo.py = 0;
            leo.update(sdt);
        } else {
            leo.frozen = true;
            leo.computePose();
        }
        if (t < 6.6f) {
            rainFrozen = false;
            updateRain(sdt, 1.0f);
        } else if (t < 10.8f) {
            rainFrozen = false;
            updateRain(sdt, -1.15f);
        } else {
            rainFrozen = true;
            updateRain(sdt, 0.0f);
        }
        break;

    case 2:
        if (t > 2.0f && t < 6.5f)
            updateGears(sdt, -3.0f);
        else
            updateGears(sdt, 1.0f);
        break;

    case 3: {
        if (t < 4.0f) {
            float u = clamp01(t / 4.0f);
            ball.frozen = true;
            ball.x = lerp(0.0f, 5.8f, u);
            ball.y = 1.0f + sinf(u * 3.1415926f) * 6.2f;
            ball.z = lerp(-18.0f, -4.8f, u);
            leo.walkPhase += sdt * 2.0f;
            leo.computePose();
        } else if (t < 6.2f) {
            ball.frozen = true;
            ball.x = 5.8f;
            ball.y = 1.4f;
            ball.z = -4.8f;
        } else if (t < 8.6f) {
            float u = clamp01((t - 6.2f) / 2.4f);
            ball.frozen = true;
            ball.x = lerp(5.8f, 0.8f, u);
            ball.y = lerp(1.4f, 5.8f, u) + sinf(u * 3.1415926f) * 0.9f;
            ball.z = lerp(-4.8f, -12.0f, u);
        } else if (t < 13.0f) {
            ball.frozen = true;
            float nudgedX = (t < 10.6f) ? 0.8f : ((t < 11.4f) ? lerp(0.8f, 3.0f, clamp01((t - 10.6f) / 0.8f)) : 3.0f);
            ball.x = nudgedX;
            ball.y = 5.8f;
            ball.z = -12.0f;
        } else {
            float u = clamp01((t - 13.0f) / 5.0f);
            ball.frozen = true;
            ball.x = lerp(3.0f, 6.2f, u);
            ball.y = lerp(5.8f, 1.0f, u) + sinf((1.0f - u) * 3.1415926f) * 0.6f;
            ball.z = lerp(-12.0f, -4.0f, u);
        }
        break;
    }

    case 4:
        leo.px = lerp(-10.0f, -2.0f, clamp01(t / 7.0f));
        leo.update(sdt);
        updateRain(sdt);
        break;

    case 5:
        // nothing dynamic
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  GLUT CALLBACKS
// ─────────────────────────────────────────────────────────────────────────────

void display() {
    switch (gScene) {
    case 0: drawScene0(); break;
    case 1: drawScene1(); break;
    case 2: drawScene2(); break;
    case 3: drawScene3(); break;
    case 4: drawScene4(); break;
    case 5: drawScene5(); break;
    default:
        glClearColor(0,0,0,1);
        glClear(GL_COLOR_BUFFER_BIT);
        begin2D();
        drawTextCentered(W/2.0f, H/2.0f, "END --  24: The OpenGL Cut", 0.8f, 0.8f, 0.8f, 1.0f, GLUT_BITMAP_HELVETICA_18);
        end2D();
        break;
    }

    // Scene transition fade-in
    float fadeInAlpha = 1.0f - smoothstep(clamp01(gTime / 0.8f));
    if (fadeInAlpha > 0.01f) {
        begin2D();
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(0, 0, 0, fadeInAlpha);
        glBegin(GL_QUADS);
        glVertex2f(0,0); glVertex2f(W,0); glVertex2f(W,H); glVertex2f(0,H);
        glEnd();
        end2D();
    }

    // Scene fade-out near end
    float scDur = (gScene < NUM_SCENES) ? FILM_SCENE_DUR[gScene] : 5.0f;
    float fadeOutAlpha = smoothstep(clamp01((gTime - (scDur - 0.8f)) / 0.8f));
    if (fadeOutAlpha > 0.01f) {
        begin2D();
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(0, 0, 0, fadeOutAlpha);
        glBegin(GL_QUADS);
        glVertex2f(0,0); glVertex2f(W,0); glVertex2f(W,H); glVertex2f(0,H);
        glEnd();
        end2D();
    }

    glutSwapBuffers();
}

void timer(int) {
    if (!gPaused) {
        float dt = 0.016f;
        gTime       += dt;
        gGlobalTime += dt;

        updateScene(dt);

        // Auto-advance
        if (gScene < NUM_SCENES && gTime >= FILM_SCENE_DUR[gScene]) {
            gScene++;
            initScene(gScene);
        }
    }
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

void keyboard(unsigned char key, int, int) {
    if (key == ' ') {
        gScene++;
        if (gScene < NUM_SCENES) initScene(gScene);
    }
    if (key == 'p' || key == 'P') gPaused = !gPaused;
    if (key == 27) exit(0); // ESC
}

void reshape(int w, int h) {
    glViewport(0, 0, w, h);
}

// ─────────────────────────────────────────────────────────────────────────────
//  MAIN
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    srand((unsigned)time(nullptr));

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowSize(W, H);
    glutCreateWindow("24 : The OpenGL Cut  |  by Karthik & Rohith");

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glEnable(GL_DEPTH_TEST);

    initSceneTextures();
    initScene(0);
    initRain();
    initGears();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutTimerFunc(16, timer, 0);

    printf("\n");
    printf("  ╔══════════════════════════════════════════╗\n");
    printf("  ║    24 : THE OPENGL CUT                   ║\n");
    printf("  ║    by Karthik & Rohith                   ║\n");
    printf("  ╠══════════════════════════════════════════╣\n");
    printf("  ║  SPACE  →  Skip to next scene            ║\n");
    printf("  ║  P      →  Pause / Unpause               ║\n");
    printf("  ║  ESC    →  Quit                          ║\n");
    printf("  ╚══════════════════════════════════════════╝\n\n");

    glutMainLoop();
    shutdownTextureAssets();
    return 0;
}
