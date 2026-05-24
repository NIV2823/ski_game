/*
 * Ski Adventure - First Person View
 * Visual style inspired by the classic mobile game "Ski Safari"
 *
 * Controls:
 *   A / Left  - Lean left
 *   D / Right - Lean right
 *   Space     - Jump
 *   ESC       - Quit
 */

#define _USE_MATH_DEFINES
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <vector>
#include <random>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

 // ─────────────────────────────────────────────
 //  SHADER SOURCES (视觉升级版)
 // ─────────────────────────────────────────────

const char* sceneVS = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 normalMatrix;

out vec3 FragPos;
out vec3 Normal;
out vec3 Color;

void main(){
    vec4 worldPos = model * vec4(aPos, 1.0);
    FragPos = worldPos.xyz;
    Normal  = mat3(normalMatrix) * aNormal;
    Color   = aColor;
    gl_Position = projection * view * worldPos;
}
)glsl";

// 升级版 Fragment Shader（雾效 + 雪地细节 + 光照优化）
const char* sceneFS = R"glsl(
#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec3 Color;

out vec4 FragColor;

uniform vec3 lightDir;
uniform vec3 viewPos;
uniform vec3 fogColor;

void main(){
    vec3 n = normalize(Normal);

    // 更真实光照
    vec3 ambient = 0.25 * Color;

    float df = max(dot(n, lightDir), 0.0);
    vec3 diffuse = df * Color * 1.2;

    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, n);

    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
    vec3 specular = 0.4 * spec * vec3(1.0);

    vec3 result = ambient + diffuse + specular;

    // 雪地噪声（假纹理）
    float noise = fract(sin(dot(FragPos.xz ,vec2(12.9898,78.233))) * 43758.5453);
    result *= 0.95 + noise * 0.1;

    // 指数雾（高级）
    float dist = length(viewPos - FragPos);
    float fogDensity = 0.025;
    float fogFactor = exp(-pow(dist * fogDensity, 2.0));
    fogFactor = clamp(fogFactor, 0.0, 1.0);

    result = mix(fogColor, result, fogFactor);

    FragColor = vec4(result, 1.0);
}
)glsl";

const char* hudVS = R"glsl(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 UV;
uniform mat4 ortho;
void main(){
    UV = aUV;
    gl_Position = ortho * vec4(aPos, 0.0, 1.0);
}
)glsl";

const char* hudFS = R"glsl(
#version 330 core
in vec2 UV;
out vec4 FragColor;
uniform vec4 color;
void main(){
    FragColor = color;
}
)glsl";

// ─────────────────────────────────────────────
//  GEOMETRY HELPERS
// ─────────────────────────────────────────────

struct MeshData {
    std::vector<float>        vertices;
    std::vector<unsigned int> indices;
};

static bool isValidNormal(glm::vec3 n) {
    return !glm::isnan(n.x) && !glm::isinf(n.x) && glm::length(n) > 0.001f;
}

static void addQuad(MeshData& m,
    glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
    glm::vec3 color)
{
    if (a == b || b == c || c == d || d == a) return;

    glm::vec3 ab = b - a;
    glm::vec3 ac = c - a;
    glm::vec3 n = glm::cross(ab, ac);

    if (glm::length(n) < 0.001f) return;
    n = glm::normalize(n);
    if (!isValidNormal(n)) return;

    unsigned int base = (unsigned int)(m.vertices.size() / 9);
    auto push = [&](glm::vec3 p) {
        m.vertices.insert(m.vertices.end(), { p.x,p.y,p.z, n.x,n.y,n.z, color.x,color.y,color.z });
        };
    push(a); push(b); push(c); push(d);
    m.indices.insert(m.indices.end(), { base, base + 1, base + 2, base + 2, base + 3, base });
}

static MeshData buildBox(glm::vec3 half, glm::vec3 col)
{
    MeshData m;
    glm::vec3 v[8] = {
        {-half.x,-half.y, half.z},{ half.x,-half.y, half.z},
        { half.x, half.y, half.z},{-half.x, half.y, half.z},
        {-half.x,-half.y,-half.z},{ half.x,-half.y,-half.z},
        { half.x, half.y,-half.z},{-half.x, half.y,-half.z}
    };
    addQuad(m, v[0], v[1], v[2], v[3], col);
    addQuad(m, v[5], v[4], v[7], v[6], col);
    addQuad(m, v[1], v[5], v[6], v[2], col);
    addQuad(m, v[4], v[0], v[3], v[7], col);
    addQuad(m, v[3], v[2], v[6], v[7], col * 1.1f);
    addQuad(m, v[4], v[5], v[1], v[0], col * 0.7f);
    return m;
}

// 升级版树颜色（更自然）
static MeshData buildTree(float scale)
{
    MeshData m;
    auto trunk = buildBox(glm::vec3(0.08f, 0.35f, 0.08f) * scale,
        glm::vec3(0.42f, 0.26f, 0.13f));
    unsigned int off = 0;
    for (auto v : trunk.vertices) m.vertices.push_back(v);
    for (auto i : trunk.indices)  m.indices.push_back(i + off);
    off = (unsigned int)(m.vertices.size() / 9);

    auto addLayer = [&](float y, float s, glm::vec3 col) {
        unsigned int base2 = (unsigned int)(m.vertices.size() / 9);
        auto layer = buildBox(glm::vec3(s, s * 0.7f, s) * scale, col);
        for (int vi = 0; vi < (int)layer.vertices.size(); vi += 9) {
            layer.vertices[vi + 1] += y * scale;
        }
        for (auto v : layer.vertices) m.vertices.push_back(v);
        for (auto i : layer.indices)  m.indices.push_back(i + base2);
        };

    // 升级版树颜色（更深、更真实）
    glm::vec3 darkGreen(0.03f, 0.25f, 0.08f);
    glm::vec3 midGreen(0.06f, 0.40f, 0.12f);
    glm::vec3 litGreen(0.12f, 0.60f, 0.20f);

    addLayer(0.35f, 0.55f, darkGreen);
    addLayer(0.80f, 0.42f, midGreen);
    addLayer(1.15f, 0.28f, litGreen);
    addLayer(1.42f, 0.15f, litGreen);
    return m;
}

static MeshData buildRock(float w, float h, float d)
{
    return buildBox(glm::vec3(w, h, d), glm::vec3(0.52f, 0.52f, 0.54f));
}

static MeshData buildPole(glm::vec3 col)
{
    return buildBox(glm::vec3(0.06f, 0.9f, 0.06f), col);
}

static MeshData buildMound()
{
    return buildBox(glm::vec3(0.5f, 0.25f, 0.35f), glm::vec3(0.92f, 0.95f, 1.0f));
}

// ─────────────────────────────────────────────
//  GPU MESH
// ─────────────────────────────────────────────

struct GpuMesh {
    unsigned int VAO = 0, VBO = 0, EBO = 0;
    int indexCount = 0;

    void upload(const MeshData& md) {
        if (md.vertices.empty() || md.indices.empty()) return;
        indexCount = (int)md.indices.size();
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, md.vertices.size() * sizeof(float),
            md.vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, md.indices.size() * sizeof(unsigned int),
            md.indices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
    }

    void draw() const {
        if (VAO && indexCount > 0) {
            glBindVertexArray(VAO);
            glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
        }
    }

    void destroy() {
        if (VAO) {
            glDeleteVertexArrays(1, &VAO);
            glDeleteBuffers(1, &VBO);
            glDeleteBuffers(1, &EBO);
            VAO = VBO = EBO = 0;
        }
    }
};

// ─────────────────────────────────────────────
//  SHADER COMPILE
// ─────────────────────────────────────────────

static unsigned int compileShader(const char* vs, const char* fs) {
    auto compile = [](GLenum type, const char* src) {
        unsigned int s = glCreateShader(type);
        glShaderSource(s, 1, &src, NULL);
        glCompileShader(s);
        int ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512]; glGetShaderInfoLog(s, 512, NULL, log);
            std::cerr << "Shader error:\n" << log << "\n";
        }
        return s;
        };
    unsigned int v = compile(GL_VERTEX_SHADER, vs);
    unsigned int f = compile(GL_FRAGMENT_SHADER, fs);
    unsigned int p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

// ─────────────────────────────────────────────
//  WORLD OBJECTS
// ─────────────────────────────────────────────

enum ObstacleType { OBS_ROCK, OBS_MOUND, OBS_POLE_RED, OBS_POLE_BLUE, OBS_TREE_OBS };

struct WorldObject {
    glm::vec3   pos;
    float       scale;
    ObstacleType type;
    bool        active;
    bool        isObstacle;
};

// ─────────────────────────────────────────────
//  GAME
// ─────────────────────────────────────────────

class SkiGame {
public:
    static constexpr int   SCR_W = 1280;
    static constexpr int   SCR_H = 720;
    static constexpr float LANE_W = 4.5f;
    static constexpr float TILE_D = 20.0f;
    static constexpr int   NUM_TILES = 8;

    float  lane = 0.0f;
    float  jumpH = 0.0f;
    float  jumpT = 0.0f;
    bool   jumping = false;
    float  speed = 8.0f;
    float  score = 0.0f;
    int    lives = 3;
    bool   gameOver = false;
    float  hitCooldown = 0.0f;

    float slopePitch = -10.0f;
    float lastFrame = 0.0f;
    float spawnTimer = 0.0f;
    float treeTimer = 0.0f;
    float slopeScroll = 0.0f;

    GLFWwindow* window = nullptr;
    unsigned int sceneProg = 0;
    unsigned int hudProg = 0;
    unsigned int hudVAO = 0;
    unsigned int hudVBO = 0;

    GpuMesh meshSnowTile;
    GpuMesh meshSkybox;
    GpuMesh meshTree;
    GpuMesh meshRock;
    GpuMesh meshMound;
    GpuMesh meshPoleRed;
    GpuMesh meshPoleBlue;
    GpuMesh meshSkisLeft;
    GpuMesh meshSkisRight;
    GpuMesh meshSkiPole;

    std::vector<WorldObject> objects;
    std::mt19937 rng{ std::random_device{}() };

    bool init()
    {
#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
#endif

        if (!glfwInit()) return false;
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_SAMPLES, 4);

        window = glfwCreateWindow(SCR_W, SCR_H, "Ski Adventure - First Person", NULL, NULL);
        if (!window) { glfwTerminate(); return false; }
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return false;

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_MULTISAMPLE);

        sceneProg = compileShader(sceneVS, sceneFS);
        hudProg = compileShader(hudVS, hudFS);

        buildMeshes();
        buildHudVAO();
        populateInitialObjects();

        lastFrame = (float)glfwGetTime();
        return true;
    }

    void buildMeshes()
    {
        // 升级版雪地颜色 (更亮更真实)
        {
            MeshData md;
            glm::vec3 snowCol(0.92f, 0.96f, 1.0f);
            glm::vec3 a(-12.0f, 0.0f, 0.0f);
            glm::vec3 b(12.0f, 0.0f, 0.0f);
            glm::vec3 c(12.0f, 0.0f, -TILE_D);
            glm::vec3 d(-12.0f, 0.0f, -TILE_D);
            addQuad(md, a, b, c, d, snowCol);
            glm::vec3 trackCol(0.85f, 0.90f, 0.96f);
            addQuad(md,
                glm::vec3(-0.08f, 0.002f, 0.0f), glm::vec3(0.08f, 0.002f, 0.0f),
                glm::vec3(0.08f, 0.002f, -TILE_D), glm::vec3(-0.08f, 0.002f, -TILE_D),
                trackCol);
            meshSnowTile.upload(md);
        }

        // 天空盒
        {
            MeshData md;
            glm::vec3 skyTop(0.40f, 0.65f, 0.95f);
            glm::vec3 skyHoriz(0.72f, 0.85f, 0.98f);
            float S = 80.0f;
            addQuad(md, { -S, S,-S }, { S, S,-S }, { S, S, S }, { -S, S, S }, skyTop);
            addQuad(md, { -S,-S,-S }, { S,-S,-S }, { S, S,-S }, { -S, S,-S }, skyHoriz);
            addQuad(md, { S,-S, S }, { -S,-S, S }, { -S, S, S }, { S, S, S }, skyHoriz);
            addQuad(md, { -S,-S, S }, { -S,-S,-S }, { -S, S,-S }, { -S, S, S }, skyHoriz);
            addQuad(md, { S,-S,-S }, { S,-S, S }, { S, S, S }, { S, S,-S }, skyHoriz);
            meshSkybox.upload(md);
        }

        meshTree.upload(buildTree(1.0f));
        meshRock.upload(buildRock(0.45f, 0.28f, 0.38f));
        meshMound.upload(buildMound());
        meshPoleRed.upload(buildPole(glm::vec3(0.9f, 0.15f, 0.15f)));
        meshPoleBlue.upload(buildPole(glm::vec3(0.15f, 0.30f, 0.90f)));

        // 滑雪板（缩短版）
        {
            MeshData leftSki, rightSki;
            glm::vec3 skiCol(0.12f, 0.18f, 0.65f);
            leftSki = buildBox(glm::vec3(0.055f, 0.018f, 0.35f), skiCol);
            rightSki = buildBox(glm::vec3(0.055f, 0.018f, 0.35f), skiCol);
            meshSkisLeft.upload(leftSki);
            meshSkisRight.upload(rightSki);
        }

        // 滑雪杖（缩短版）
        {
            MeshData pole = buildBox(glm::vec3(0.02f, 0.25f, 0.02f), glm::vec3(0.6f, 0.6f, 0.6f));
            meshSkiPole.upload(pole);
        }
    }

    void buildHudVAO()
    {
        float quadVerts[] = {
            0,0, 0,0,
            1,0, 1,0,
            1,1, 1,1,
            0,0, 0,0,
            1,1, 1,1,
            0,1, 0,1,
        };
        glGenVertexArrays(1, &hudVAO);
        glGenBuffers(1, &hudVBO);
        glBindVertexArray(hudVAO);
        glBindBuffer(GL_ARRAY_BUFFER, hudVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
    }

    void populateInitialObjects()
    {
        for (int i = 0; i < 60; i++) {
            spawnTree(-rand_range(5.0f, 150.0f));
        }
    }

    float rand_range(float lo, float hi) {
        std::uniform_real_distribution<float> d(lo, hi);
        return d(rng);
    }

    void spawnTree(float zPos = -40.0f) {
        WorldObject o;
        float side = (rng() & 1) ? 1.0f : -1.0f;
        o.pos = glm::vec3(side * rand_range(3.0f, 10.0f), 0.0f, zPos);
        o.scale = rand_range(0.7f, 1.8f);
        o.type = OBS_TREE_OBS;
        o.active = true;
        o.isObstacle = false;
        objects.push_back(o);
    }

    void spawnObstacle() {
        WorldObject o;
        o.pos = glm::vec3(rand_range(-3.5f, 3.5f), 0.0f, -38.0f);
        o.scale = rand_range(0.8f, 1.5f);
        o.active = true;
        o.isObstacle = true;

        int r = rng() % 10;
        if (r < 4)      o.type = OBS_ROCK;
        else if (r < 6) o.type = OBS_MOUND;
        else if (r < 8) o.type = OBS_POLE_RED;
        else            o.type = OBS_POLE_BLUE;

        objects.push_back(o);
    }

    void update(float dt)
    {
        if (gameOver) {
            if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS) restartGame();
            return;
        }

        float steer = 0.0f;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) steer = -1.0f;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) steer = 1.0f;

        float turnSpeed = 5.5f + speed * 0.08f;
        lane += steer * turnSpeed * dt;
        lane = glm::clamp(lane, -LANE_W, LANE_W);

        if ((glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) && !jumping) {
            jumping = true;
            jumpT = 0.0f;
        }
        if (jumping) {
            jumpT += dt * 3.2f;
            jumpH = sinf(jumpT * (float)M_PI) * 1.8f;
            if (jumpT >= 1.0f) { jumping = false; jumpH = 0.0f; }
        }

        slopeScroll += speed * dt;
        for (auto& o : objects) {
            o.pos.z += speed * dt;
        }

        objects.erase(std::remove_if(objects.begin(), objects.end(),
            [](const WorldObject& o) { return o.pos.z > 8.0f || !o.active; }),
            objects.end());

        treeTimer += dt;
        if (treeTimer > 0.35f) {
            spawnTree();
            treeTimer = 0.0f;
        }

        spawnTimer += dt;
        float spawnInterval = glm::mix(2.5f, 0.7f, glm::clamp((speed - 8.0f) / 14.0f, 0.0f, 1.0f));
        if (spawnTimer > spawnInterval) {
            spawnObstacle();
            spawnTimer = 0.0f;
        }

        hitCooldown = (std::max)(0.0f, hitCooldown - dt);
        if (hitCooldown <= 0.0f && !jumping) {
            for (auto& o : objects) {
                if (!o.active) continue;

                float dx = fabsf(o.pos.x - lane);
                float dz = fabsf(o.pos.z);
                bool isPole = (o.type == OBS_POLE_RED || o.type == OBS_POLE_BLUE);
                float hitR = isPole ? 0.6f : 0.7f;

                if (dx < hitR && dz < 1.2f) {
                    if (isPole) {
                        score += 20.0f;
                        o.active = false;
                        continue;
                    }

                    o.active = false;
                    lives--;
                    score = (std::max)(0.0f, score - 100.0f);
                    speed = (std::max)(5.0f, speed - 3.0f);
                    hitCooldown = 2.5f;
                    if (lives <= 0) gameOver = true;
                    break;
                }
            }
        }

        score += dt * speed * 8.0f;
        if (speed < 22.0f) speed += dt * 0.6f;
    }

    void render()
    {
        bool flashing = hitCooldown > 0.0f && ((int)(hitCooldown * 6) % 2 == 0);
        glClearColor(flashing ? 0.6f : 0.40f, 0.45f, 0.55f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float camX = lane;
        // 跳跃镜头晃动
        float camY = 1.55f + jumpH + sin(glfwGetTime() * 20.0f) * 0.02f * jumpH;
        float camZ = 0.0f;
        glm::vec3 camPos(camX, camY, camZ);

        // 升级版镜头倾斜（更真实）
        float leanAngle = -lane * 6.0f;
        glm::mat4 camRot = glm::rotate(glm::mat4(1.0f), glm::radians(slopePitch + jumpH * (-4.0f)), glm::vec3(1, 0, 0));
        camRot = glm::rotate(camRot, glm::radians(leanAngle), glm::vec3(0, 0, 1));
        glm::vec3 front = glm::vec3(camRot * glm::vec4(0, 0, -1, 0));
        glm::vec3 up = glm::vec3(camRot * glm::vec4(0, 1, 0, 0));

        glm::mat4 view = glm::lookAt(camPos, camPos + front, up);
        glm::mat4 proj = glm::perspective(glm::radians(80.0f), (float)SCR_W / SCR_H, 0.05f, 160.0f);

        glm::vec3 lightDir = glm::normalize(glm::vec3(0.4f, 1.0f, 0.5f));
        // 升级版雾颜色（更真实）
        glm::vec3 fogColor(0.78f, 0.88f, 1.0f);

        glUseProgram(sceneProg);
        setUniformMat4("view", view);
        setUniformMat4("projection", proj);
        setUniform3f("lightDir", lightDir);
        setUniform3f("viewPos", camPos);
        setUniform3f("fogColor", fogColor);

        // 天空盒
        glDepthMask(GL_FALSE);
        {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(camX, 0, -30));
            setUniformMat4("model", model);
            setUniformMat4("normalMatrix", glm::transpose(glm::inverse(model)));
            meshSkybox.draw();
        }
        glDepthMask(GL_TRUE);

        // 雪地（带地形起伏）
        {
            float scrollMod = fmodf(slopeScroll, TILE_D);
            for (int i = 0; i < NUM_TILES; i++) {
                float tz = -scrollMod + (i - 1) * TILE_D;
                // 地形起伏
                float height = sin((tz + slopeScroll) * 0.05f) * 0.3f;
                glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, height, tz));
                setUniformMat4("model", model);
                setUniformMat4("normalMatrix", glm::transpose(glm::inverse(model)));
                meshSnowTile.draw();
            }
        }

        // 世界物体
        for (const auto& o : objects) {
            if (!o.active) continue;
            drawObject(o);
        }

        drawSkis(camPos);
        renderHUD();
    }

    void drawObject(const WorldObject& o)
    {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), o.pos);

        // 树随机旋转（用位置做伪随机）
        if (o.type == OBS_TREE_OBS) {
            model = glm::rotate(model, o.pos.x * 2.5f, glm::vec3(0, 1, 0));
        }

        model = glm::scale(model, glm::vec3(o.scale));
        setUniformMat4("model", model);
        setUniformMat4("normalMatrix", glm::transpose(glm::inverse(model)));

        switch (o.type) {
        case OBS_TREE_OBS:  meshTree.draw();     break;
        case OBS_ROCK:      meshRock.draw();     break;
        case OBS_MOUND:     meshMound.draw();    break;
        case OBS_POLE_RED:  meshPoleRed.draw();  break;
        case OBS_POLE_BLUE: meshPoleBlue.draw(); break;
        }
    }

    void drawSkis(glm::vec3 camPos)
    {
        float t = (float)glfwGetTime();
        float bob = sinf(t * 12.0f) * 0.012f * (fabsf(lane) > 0.1f ? 1.0f : 0.0f);
        glm::vec3 skiPos = camPos + glm::vec3(0.0f, -0.28f + bob, -0.6f);

        glm::mat4 model;

        model = glm::translate(glm::mat4(1.0f), skiPos + glm::vec3(-0.18f, 0.0f, 0.0f));
        setUniformMat4("model", model);
        setUniformMat4("normalMatrix", glm::transpose(glm::inverse(model)));
        meshSkisLeft.draw();

        model = glm::translate(glm::mat4(1.0f), skiPos + glm::vec3(0.18f, 0.0f, 0.0f));
        setUniformMat4("model", model);
        setUniformMat4("normalMatrix", glm::transpose(glm::inverse(model)));
        meshSkisRight.draw();

        glm::vec3 polePosL = camPos + glm::vec3(-0.28f, -0.22f, -0.45f);
        model = glm::translate(glm::mat4(1.0f), polePosL);
        setUniformMat4("model", model);
        setUniformMat4("normalMatrix", glm::transpose(glm::inverse(model)));
        meshSkiPole.draw();

        glm::vec3 polePosR = camPos + glm::vec3(0.28f, -0.22f, -0.45f);
        model = glm::translate(glm::mat4(1.0f), polePosR);
        setUniformMat4("model", model);
        setUniformMat4("normalMatrix", glm::transpose(glm::inverse(model)));
        meshSkiPole.draw();
    }

    void renderHUD()
    {
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glm::mat4 ortho = glm::ortho(0.0f, (float)SCR_W, 0.0f, (float)SCR_H);
        glUseProgram(hudProg);
        glUniformMatrix4fv(glGetUniformLocation(hudProg, "ortho"), 1, GL_FALSE, glm::value_ptr(ortho));
        glBindVertexArray(hudVAO);

        drawHudRect(0, SCR_H - 52, SCR_W, 52, glm::vec4(0.0f, 0.0f, 0.0f, 0.45f));

        float barW = glm::clamp(score / 5000.0f, 0.0f, 1.0f) * 320.0f;
        drawHudRect(16, SCR_H - 44, 320, 14, glm::vec4(0.2f, 0.2f, 0.2f, 0.7f));
        if (barW > 0) {
            drawHudRect(16, SCR_H - 44, (int)barW, 14, glm::vec4(1.0f, 0.82f, 0.0f, 0.9f));
        }

        float spdFrac = glm::clamp((speed - 5.0f) / 17.0f, 0.0f, 1.0f);
        float spdW = spdFrac * 200.0f;
        drawHudRect(16, SCR_H - 26, 200, 10, glm::vec4(0.2f, 0.2f, 0.2f, 0.7f));
        if (spdW > 0) {
            drawHudRect(16, SCR_H - 26, (int)spdW, 10, glm::vec4(0.15f, 0.85f, 0.95f, 0.9f));
        }

        for (int i = 0; i < lives; i++) {
            drawHudRect(SCR_W - 30 - i * 22, SCR_H - 38, 16, 16,
                glm::vec4(0.95f, 0.18f, 0.18f, 0.95f));
        }

        if (speed > 18.0f) {
            float flash = 0.5f + 0.5f * sinf((float)glfwGetTime() * 10.0f);
            drawHudRect(0, 0, SCR_W, 6, glm::vec4(1.0f, 0.55f, 0.0f, flash));
        }

        if (hitCooldown > 0.0f) {
            float alpha = hitCooldown / 2.5f * 0.55f;
            drawHudRect(0, 0, SCR_W, 8, glm::vec4(0.9f, 0.1f, 0.1f, alpha));
            drawHudRect(0, SCR_H - 8, SCR_W, 8, glm::vec4(0.9f, 0.1f, 0.1f, alpha));
            drawHudRect(0, 0, 8, SCR_H, glm::vec4(0.9f, 0.1f, 0.1f, alpha));
            drawHudRect(SCR_W - 8, 0, 8, SCR_H, glm::vec4(0.9f, 0.1f, 0.1f, alpha));
        }

        int cx = SCR_W / 2, cy = SCR_H / 2;
        drawHudRect(cx - 8, cy - 1, 16, 2, glm::vec4(1, 1, 1, 0.5f));
        drawHudRect(cx - 1, cy - 8, 2, 16, glm::vec4(1, 1, 1, 0.5f));

        if (gameOver) {
            drawHudRect(0, 0, SCR_W, SCR_H, glm::vec4(0.0f, 0.0f, 0.0f, 0.55f));
            drawHudRect(SCR_W / 2 - 140, SCR_H / 2 - 30, 280, 60,
                glm::vec4(0.8f, 0.1f, 0.1f, 0.85f));
        }

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);

        static float lastPrint = 0;
        float now = (float)glfwGetTime();
        if (now - lastPrint > 0.1f) {
            printf("\033[2J\033[H");

            int scoreBars = (int)((score / 5000.0f) * 20);
            int speedBars = (int)(spdFrac * 20);

            printf("+----------------------------------------------------+\n");
            printf("|                 SKI ADVENTURE                      |\n");
            printf("+----------------------------------------------------+\n");
            printf("| SCORE: %6.0f / 5000  [", score);
            for (int i = 0; i < 20; i++) printf("%c", i < scoreBars ? '#' : '-');
            printf("]  %3.0f%%\n", (score / 5000.0f) * 100.0f);
            printf("| SPEED: %5.1f / 22.0  [", speed);
            for (int i = 0; i < 20; i++) printf("%c", i < speedBars ? '#' : '-');
            printf("]  %3.0f%%\n", spdFrac * 100.0f);
            printf("| LIVES: %d / 3          ", lives);
            for (int i = 0; i < 3; i++) printf("%c", i < lives ? 'H' : '.');
            printf("\n");
            printf("+----------------------------------------------------+\n");
            printf("| CONTROLS: A/D = Move    SPACE = Jump    ESC = Exit |\n");
            printf("| TIPS: Red/Blue Poles = +20 points                 |\n");
            if (speed > 18.0f) {
                printf("| >>> SPEED BOOST ACTIVE! <<<                       |\n");
            }
            if (hitCooldown > 0.0f) {
                printf("| INVINCIBLE: %.1f seconds remaining               |\n", hitCooldown);
            }
            if (gameOver) {
                printf("| >>> GAME OVER - Press ENTER to restart <<<       |\n");
            }
            printf("+----------------------------------------------------+\n");
            fflush(stdout);
            lastPrint = now;
        }
    }

    void drawHudRect(int x, int y, int w, int h, glm::vec4 col)
    {
        if (w <= 0 || h <= 0) return;

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(x, y, 0));
        model = glm::scale(model, glm::vec3(w, h, 1));
        glm::mat4 ortho = glm::ortho(0.0f, (float)SCR_W, 0.0f, (float)SCR_H);
        glm::mat4 mvp = ortho * model;

        glUniformMatrix4fv(glGetUniformLocation(hudProg, "ortho"), 1, GL_FALSE, glm::value_ptr(mvp));
        glUniform4fv(glGetUniformLocation(hudProg, "color"), 1, glm::value_ptr(col));
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    void restartGame() {
        lane = 0; jumpH = 0; jumpT = 0; jumping = false;
        speed = 8.0f; score = 0.0f; lives = 3;
        gameOver = false; hitCooldown = 0.0f;
        spawnTimer = 0; treeTimer = 0; slopeScroll = 0;
        objects.clear();
        populateInitialObjects();
    }

    void setUniformMat4(const char* name, const glm::mat4& m) {
        glUniformMatrix4fv(glGetUniformLocation(sceneProg, name), 1, GL_FALSE, glm::value_ptr(m));
    }
    void setUniform3f(const char* name, glm::vec3 v) {
        glUniform3fv(glGetUniformLocation(sceneProg, name), 1, glm::value_ptr(v));
    }
    void setUniform1f(const char* name, float v) {
        glUniform1f(glGetUniformLocation(sceneProg, name), v);
    }

    void run()
    {
        printf("\033[2J\033[H");
        lastFrame = (float)glfwGetTime();

        while (!glfwWindowShouldClose(window)) {
            float now = (float)glfwGetTime();
            float dt = glm::clamp(now - lastFrame, 0.0f, 0.05f);
            lastFrame = now;

            glfwPollEvents();
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                glfwSetWindowShouldClose(window, true);

            update(dt);
            render();
            glfwSwapBuffers(window);
        }
    }

    ~SkiGame()
    {
        meshSnowTile.destroy();
        meshSkybox.destroy();
        meshTree.destroy();
        meshRock.destroy();
        meshMound.destroy();
        meshPoleRed.destroy();
        meshPoleBlue.destroy();
        meshSkisLeft.destroy();
        meshSkisRight.destroy();
        meshSkiPole.destroy();
        if (hudVAO) { glDeleteVertexArrays(1, &hudVAO); glDeleteBuffers(1, &hudVBO); }
        glDeleteProgram(sceneProg);
        glDeleteProgram(hudProg);
        glfwDestroyWindow(window);
        glfwTerminate();
    }
};

int main()
{
    SkiGame game;
    if (!game.init()) {
        std::cerr << "Init failed\n";
        return -1;
    }
    game.run();
    return 0;
}