#include "fireworks.h"
#include "panel.h"
#include "math.h"
#include "stdlib.h"
#include "string.h"
#include "stm32f4xx_hal.h"
#include "bridge.h"
#include "sound.h"
#include "traffic.h"

// 하드웨어 버퍼 크기 (데이터 전송용)
#define BUFFER_W 128
#define BUFFER_H 32

// 가상 물리 공간 크기 (상하 적층된 64x64 화면으로 시뮬레이션)
#define VIRTUAL_W 64
#define VIRTUAL_H 64

#define UPDATE_INTERVAL_MS 30

// 더 많이, 더 화려하게
#define MAX_ROCKETS 10
#define MAX_PARTICLES_PER_EXPLOSION 40

// 특수 텍스트 관련
#define SPECIAL_TYPES 3
static const char *special_texts[SPECIAL_TYPES] = {"CO-SHOW", "COSS", "POLARIS"};

// 폰트 5x7
static const uint8_t font5x7[27][5] = {
    {0x7C,0x12,0x11,0x12,0x7C}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x7A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x0C,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x7F,0x20,0x18,0x20,0x7F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x03,0x04,0x78,0x04,0x03}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
    {0x08,0x08,0x08,0x08,0x08}  // '-'
};

static int char_to_index(char c) {
    if (c == '-') return 26;
    if (c >= 'A' && c <= 'Z') return c - 'A';
    return -1;
}

// 프레임버퍼 (하드웨어 전송용 128x32)
static uint8_t fbR[BUFFER_H][BUFFER_W];
static uint8_t fbG[BUFFER_H][BUFFER_W];
static uint8_t fbB[BUFFER_H][BUFFER_W];

// 폭발 모양
typedef enum {
    SHAPE_CIRCLE = 0,
    SHAPE_RING,
    SHAPE_STAR,
    SHAPE_HEART
} ExplosionShape;

typedef struct {
    float x, y; // 가상 좌표 (0~63, 0~63)
    float vy;
    uint8_t color_r, color_g, color_b;
    bool alive;
    bool exploded;
    int  special_type;
    uint32_t birth_ms;
    ExplosionShape shape;
} Rocket;

typedef struct {
    float x, y; // 가상 좌표 (0~63, 0~63)
    float vx, vy;
    float life;
    float decay;
    uint8_t color_r, color_g, color_b;
    bool alive;
} Particle;

typedef struct {
    bool active;
    int  type;
    float x, y; // 가상 좌표
    float life;
} SpecialText;

static Rocket rockets[MAX_ROCKETS];
static Particle particles[MAX_ROCKETS * MAX_PARTICLES_PER_EXPLOSION];
static SpecialText special;

static uint32_t last_update_ms = 0;
static float gravity = -0.07f;

// 자동 모드 관련
static bool      auto_mode = false;
static uint32_t next_auto_spawn_ms = 0;

#define AUTO_MIN_INTERVAL_MS  700u
#define AUTO_MAX_INTERVAL_MS  2000u

static uint32_t random_interval_ms(void)
{
    uint32_t range = AUTO_MAX_INTERVAL_MS - AUTO_MIN_INTERVAL_MS + 1;
    return AUTO_MIN_INTERVAL_MS + (rand() % range);
}

static float randf(float a, float b) {
    return a + (b - a) * (float)rand() / (float)RAND_MAX;
}

void Fireworks_Init(void)
{
    memset(fbR,0,sizeof(fbR));
    memset(fbG,0,sizeof(fbG));
    memset(fbB,0,sizeof(fbB));
    memset(rockets,0,sizeof(rockets));
    memset(particles,0,sizeof(particles));
    memset(&special,0,sizeof(special));
    special.active = false;
    last_update_ms = HAL_GetTick();
    srand((unsigned)(HAL_GetTick() & 0xFFFF));

    auto_mode = false;
    next_auto_spawn_ms = 0;
    Traffic_Init();
}

void Fireworks_ToggleAuto(void)
{
    auto_mode = !auto_mode;

    if (auto_mode) {
        uint32_t now = HAL_GetTick();
        next_auto_spawn_ms = now + random_interval_ms();
    } else {
        next_auto_spawn_ms = 0;
    }
}

// 버튼 또는 EXTI에서 호출
void Fireworks_TriggerSpawn(void)
{
    float spawn_min_x = 2.0f;
    float spawn_max_x = (float)(VIRTUAL_W - 3); // 64폭 기준 여유

    for (int i=0;i<MAX_ROCKETS;i++){
        if (!rockets[i].alive){
            rockets[i].alive    = true;
            rockets[i].exploded = false;

            rockets[i].x = randf(spawn_min_x, spawn_max_x);

            // 물리적 아래 패널의 맨 아래(가상 Y=63)에서 위로 21픽셀 올라간 위치(Y=42)에서 시작
            rockets[i].y        = (float)(VIRTUAL_H - 1 - 21); // 63 - 21 = 42

            rockets[i].vy       = randf(-1.2f, -2.1f);   // 위로 쏘아올림 (Y 감소 방향)

            // 로켓 기본 꼬리 색
            int c = rand() % 4;
            switch (c) {
                case 0: rockets[i].color_r = 15; rockets[i].color_g = 6;  rockets[i].color_b = 0;  break;
                case 1: rockets[i].color_r = 12; rockets[i].color_g = 0;  rockets[i].color_b = 12; break;
                case 2: rockets[i].color_r = 0;  rockets[i].color_g = 12; rockets[i].color_b = 12; break;
                default:rockets[i].color_r = 12; rockets[i].color_g = 12; rockets[i].color_b = 3;  break;
            }

            rockets[i].birth_ms     = HAL_GetTick();
            rockets[i].special_type = ((rand() % 3) == 0) ? (rand() % SPECIAL_TYPES) : -1;
            rockets[i].shape = (ExplosionShape)(rand() % 4);

            break;
        }
    }
}

static void clear_fb(void)
{
    memset(fbR,0,sizeof(fbR));
    memset(fbG,0,sizeof(fbG));
    memset(fbB,0,sizeof(fbB));
}

// ==================== 렌더링 헬퍼 함수 ====================

// 실제 버퍼(128x32)에 직접 그리는 함수 (내부용)
static void draw_buffer_pixel(int buf_x, int buf_y, uint8_t r, uint8_t g, uint8_t b, float intensity)
{
    if (buf_x < 0 || buf_x >= BUFFER_W || buf_y < 0 || buf_y >= BUFFER_H) return;

    if (intensity <= 0.0f) return;
    if (intensity > 1.5f) intensity = 1.5f;

    int addR = (int)((float)r * intensity);
    int addG = (int)((float)g * intensity);
    int addB = (int)((float)b * intensity);

    int newR = fbR[buf_y][buf_x] + addR; if (newR>15) newR=15;
    int newG = fbG[buf_y][buf_x] + addG; if (newG>15) newG=15;
    int newB = fbB[buf_y][buf_x] + addB; if (newB>15) newB=15;

    fbR[buf_y][buf_x] = (uint8_t)newR;
    fbG[buf_y][buf_x] = (uint8_t)newG;
    fbB[buf_y][buf_x] = (uint8_t)newB;
}

// 배경: 브릿지 이미지는 128x32 버퍼 전체에 맵핑됨..!
static void draw_bridge_background(void)
{
    for (int y = 0; y < BUFFER_H; y++) {
        for (int x = 0; x < BUFFER_W; x++) {
            uint8_t r, g, b;
            Bridge_GetPixel(x, y, &r, &g, &b);
            // 배경은 이미지를 그대로 복사 (add 아님)
            fbR[y][x] = r;
            fbG[y][x] = g;
            fbB[y][x] = b;
        }
    }
}

// 가상 좌표(64x64)를 실제 하드웨어 버퍼(128x32)로 변환하여 그리자..
static void draw_virtual_pixel(float fx, float fy, uint8_t r, uint8_t g, uint8_t b, float intensity)
{
    int vx = (int)(fx + 0.5f);
    int vy = (int)(fy + 0.5f);

    // 1. 가상 공간 클리핑
    if (vx < 0 || vx >= VIRTUAL_W) return;
    if (vy < 0 || vy >= VIRTUAL_H) return;

    int real_x, real_y;

    // 2. 패널 매핑 (상하 적층 구조 )
    // Y: 0이 최상단, 63이 최하단
    if (vy < 32) {
        // [상단 패널] (Virtual Y: 0~31)
        real_x = vx;      // 0 ~ 63
        real_y = vy;      // 0 ~ 31
    } else {
        // [하단 패널] (Virtual Y: 32~63)
        real_x = vx + 64; // 64 ~ 127
        real_y = vy - 32; // 0 ~ 31
    }

    draw_buffer_pixel(real_x, real_y, r, g, b, intensity);
}


// 특수 텍스트 그리기: draw_virtual_pixel 사용
static void draw_text_centered(float cx, float cy, const char *s,
                               uint8_t r, uint8_t g, uint8_t b,
                               float life_ratio)
{
    int len = strlen(s);
    if (len <= 0) return;

    int total_w = len * 6 - 1;
    int start_x = (int)(cx - total_w/2);
    int baseline_y = (int)(cy - 3);

    if (life_ratio < 0.0f) life_ratio = 0.0f;
    if (life_ratio > 1.0f) life_ratio = 1.0f;

    for (int i=0;i<len;i++){
        int idx = char_to_index(s[i]);
        if (idx < 0) continue;
        for (int col=0;col<5;col++){
            uint8_t colbits = font5x7[idx][col];
            for (int row=0;row<7;row++){
                if (colbits & (1<<row)){
                    int px = start_x + i*6 + col;
                    int py = baseline_y + row;

                    float inten = 1.0f * life_ratio;
                    draw_virtual_pixel(px, py, r, g, b, inten);

                    float crossInt = inten * 0.35f;
                    draw_virtual_pixel(px-1, py,   r/2, g/2, b/2, crossInt);
                    draw_virtual_pixel(px+1, py,   r/2, g/2, b/2, crossInt);
                    draw_virtual_pixel(px,   py-1, r/2, g/2, b/2, crossInt);
                    draw_virtual_pixel(px,   py+1, r/2, g/2, b/2, crossInt * 0.8f);
                }
            }
        }
    }
}

// ==================== 2단 폭발 트리거 ====================
static void trigger_second_stage(const Particle *seed)
{
    int max_spawn = 24;
    int spawned   = 0;

    float cx = seed->x;
    float cy = seed->y;
    uint8_t cr = seed->color_r;
    uint8_t cg = seed->color_g;
    uint8_t cb = seed->color_b;

    for (int i=0;
         i<MAX_ROCKETS * MAX_PARTICLES_PER_EXPLOSION && spawned < max_spawn;
         i++) {

        if (particles[i].alive) continue;

        particles[i].alive = true;
        particles[i].x = cx;
        particles[i].y = cy;

        float ang, speed;

        if (seed->decay < 1.5f) {
            int branch = rand() % 5;
            float base = (2.0f * 3.1415926f / 5.0f) * (float)branch;
            float jitter = randf(-0.20f, 0.20f);
            ang   = base + jitter;
            speed = randf(1.5f, 2.5f);
            particles[i].vx = cosf(ang) * speed;
            particles[i].vy = sinf(ang) * speed * 0.9f;
        } else {
            ang   = randf(0.0f, 2.0f * 3.1415926f);
            speed = randf(0.5f, 1.5f);
            particles[i].vx = cosf(ang) * speed;
            particles[i].vy = sinf(ang) * speed * 0.8f;
        }

        particles[i].life  = randf(0.5f, 1.0f);
        particles[i].decay = 0.0f;

        particles[i].color_r = cr;
        particles[i].color_g = cg;
        particles[i].color_b = cb;

        spawned++;
    }
}

static void explode_rocket(Rocket *r)
{
    if (!r->alive || r->exploded) return;

    // 특수 텍스트
    if (r->special_type >= 0) {
        special.active = true;
        special.type   = r->special_type;
        special.x      = r->x;
        special.y      = r->y;
        special.life   = 1.6f;
        r->exploded = true;
        r->alive    = false;
        Sound_PlayFirework();
        return;
    }

    // 일반 파티클 팔레트
    uint8_t c1r, c1g, c1b;
    uint8_t c2r, c2g, c2b;
    int paletteSize = 2;

    switch (rand() % 4) {
        case 0: c1r = 15; c1g = 10; c1b = 0;  c2r = 15; c2g = 5;  c2b = 0; break;
        case 1: c1r = 15; c1g = 0;  c1b = 0;  c2r = 15; c2g = 0;  c2b = 8; break;
        case 2: c1r = 0;  c1g = 10; c1b = 15; c2r = 0;  c2g = 5;  c2b = 15; break;
        default:c1r = 10; c1g = 15; c1b = 0;  c2r = 4;  c2g = 15; c2b = 0; break;
    }
    if (rand() % 4 == 0) paletteSize = 1;

    int spawned = 0;
    for (int i = 0;
         i < MAX_ROCKETS * MAX_PARTICLES_PER_EXPLOSION && spawned < MAX_PARTICLES_PER_EXPLOSION;
         i++) {

        if (particles[i].alive) continue;

        particles[i].alive = true;
        particles[i].x = r->x;
        particles[i].y = r->y;

        float ang = 0.0f, speed = 0.0f;
        float lifeMin = 0.7f, lifeMax = 1.8f;
        float seedDecay = 0.0f;

        switch (r->shape) {
        case SHAPE_RING:
            ang   = randf(0.0f, 2.0f * 3.1415926f);
            speed = randf(1.6f, 2.6f);
            particles[i].vx = cosf(ang) * speed;
            particles[i].vy = sinf(ang) * speed * 0.7f;
            lifeMin = 0.9f; lifeMax = 1.5f;
            break;

        case SHAPE_STAR:
            {
                int branch = rand() % 5;
                float base = (2.0f * 3.1415926f / 5.0f) * (float)branch;
                bool outerPhase = (rand() % 3 == 0);

                if (!outerPhase) {
                    float jitter = randf(-0.08f, 0.08f);
                    ang   = base + jitter;
                    speed = randf(2.2f, 3.2f);
                    lifeMin = 0.8f; lifeMax = 1.2f;
                } else {
                    float jitter = randf(-0.35f, 0.35f);
                    ang   = base + jitter;
                    speed = randf(2.2f, 3.6f);
                    lifeMin = 1.4f; lifeMax = 2.2f;
                    if (rand() % 3 == 0) {
                        seedDecay = 1.0f;
                        lifeMin = 0.7f; lifeMax = 1.0f;
                    }
                }
                particles[i].vx = cosf(ang) * speed;
                particles[i].vy = sinf(ang) * speed * 0.85f;
            }
            break;

        case SHAPE_HEART:
            {
                float t = randf(0.0f, 2.0f * 3.1415926f);
                float hx = 16.0f * sinf(t) * sinf(t) * sinf(t);
                float hy = 13.0f * cosf(t) - 5.0f * cosf(2.0f*t) - 2.0f * cosf(3.0f*t) - cosf(4.0f*t);
                float scale = 0.1f;
                particles[i].vx = hx * scale;
                particles[i].vy = -hy * scale * 0.65f;
                lifeMin = 0.9f; lifeMax = 1.7f;
            }
            break;

        case SHAPE_CIRCLE:
        default:
            {
                bool outerPhase = (rand() % 4 == 0);
                ang = randf(0.0f, 2.0f * 3.1415926f);
                if (!outerPhase) {
                    speed   = randf(0.6f, 1.6f);
                    lifeMin = 0.8f; lifeMax = 1.4f;
                } else {
                    speed   = randf(1.6f, 2.8f);
                    lifeMin = 1.4f; lifeMax = 2.2f;
                    if (rand() % 4 == 0) {
                        seedDecay = 2.0f;
                        lifeMin = 0.7f; lifeMax = 1.0f;
                    }
                }
                particles[i].vx = cosf(ang) * speed;
                particles[i].vy = sinf(ang) * speed * 0.7f + randf(-0.2f, 0.2f);
            }
            break;
        }

        particles[i].life  = randf(lifeMin, lifeMax);
        particles[i].decay = seedDecay;

        if (paletteSize == 1 || (rand() % 2 == 0)) {
            particles[i].color_r = c1r; particles[i].color_g = c1g; particles[i].color_b = c1b;
        } else {
            particles[i].color_r = c2r; particles[i].color_g = c2g; particles[i].color_b = c2b;
        }

        spawned++;
    }

    r->exploded = true;
    r->alive    = false;
    Sound_PlayFirework();
}


void Fireworks_Update(uint32_t ms)
{


    if (ms - last_update_ms < UPDATE_INTERVAL_MS) return;

    uint32_t steps = (ms - last_update_ms) / UPDATE_INTERVAL_MS;
    if (steps == 0) steps = 1;

    for (uint32_t s=0;s<steps;s++){
        float dt = (float)UPDATE_INTERVAL_MS / 1000.0f;

        // 로켓 업데이트 (가상 좌표계 VIRTUAL_H 기준)
        for (int i = 0; i < MAX_ROCKETS; i++) {
            if (!rockets[i].alive) continue;

            rockets[i].vy -= gravity * dt;
            rockets[i].y  += rockets[i].vy * dt * 10.0f;

            // VIRTUAL_H (64) 기준으로 고도 계산
            float altitude = (float)(VIRTUAL_H - 1) - rockets[i].y;
            if (altitude < 0.0f) altitude = 0.0f;
            float altitude_norm = altitude / (float)(VIRTUAL_H - 1);
            if (altitude_norm > 1.0f) altitude_norm = 1.0f;

            // ===================== 폭발 확률 재조정 =====================
            // 아래쪽(altitude_norm < 0.5)에서는 거의 안 터지게,
            // 위쪽으로 갈수록 급격히 터질 확률이 올라가도록 곡선 적용
            float baseP   = 0.0015f;   // 전체적으로 아주 약한 기본 확률
            float extraMax = 0.25f;    // 위에서 추가되는 최대 확률

            float heightFactor;
            if (altitude_norm < 0.5f) {
                // 화면 아래 절반에서는 랜덤 폭발 거의 없음
                heightFactor = 0.0f;
            } else {
                // 상단 절반(0.5~1.0) 구간을 [0,1]로 정규화 후 제곱으로 상단 쪽에 몰리게
                float t = (altitude_norm - 0.5f) / 0.5f; // 0~1
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                heightFactor = t * t;
            }

            float explodeP = baseP + extraMax * heightFactor;
            // ========================================================

            if (rockets[i].alive && randf(0.0f, 1.0f) < explodeP) {
                explode_rocket(&rockets[i]);
            }

            // 화면 최상단(Y=0 근처) 도달 시 강제 폭발!!!
            if (rockets[i].alive && rockets[i].y < (VIRTUAL_H * 0.12f)) {
                explode_rocket(&rockets[i]);
            }

            // 화면 밖으로 나가면 제거 (상단 위)
            if (rockets[i].y < -5.0f) {
                rockets[i].alive = false;
            }
        }

        // 파티클 업데이트
        for (int i=0;i<MAX_ROCKETS * MAX_PARTICLES_PER_EXPLOSION;i++){
            if (!particles[i].alive) continue;

            particles[i].vy -= gravity * dt * 0.4f;
            particles[i].x  += particles[i].vx * dt * 8.0f;
            particles[i].y  += particles[i].vy * dt * 8.0f;

            particles[i].vx *= 0.995f;
            particles[i].vy *= 0.995f;

            particles[i].life -= dt;

            if (particles[i].life <= 0.0f) {
                if (particles[i].decay >= 0.5f) {
                    trigger_second_stage(&particles[i]);
                }
                particles[i].alive = false;
            }
        }

        // 특수 텍스트
        if (special.active) {
            special.life -= dt;
            if (special.life <= 0.0f) special.active = false;
        }
    }

    if (auto_mode) {
        if (next_auto_spawn_ms == 0) {
            next_auto_spawn_ms = ms + random_interval_ms();
        }
        if ((int32_t)(ms - next_auto_spawn_ms) >= 0) {
            Fireworks_TriggerSpawn();
            next_auto_spawn_ms = ms + random_interval_ms();
        }
    }
    Traffic_Update();
    last_update_ms = ms;
}

void Fireworks_Render(void)
{
    clear_fb();

    // 배경 (브릿지) - 128x32 버퍼에 직접 그림 
    draw_bridge_background();

    // 로켓 (가상 좌표 -> 실제 좌표 매핑)
    for (int i=0;i<MAX_ROCKETS;i++){
        if (!rockets[i].alive) continue;

        draw_virtual_pixel(rockets[i].x, rockets[i].y,
                           rockets[i].color_r, rockets[i].color_g, rockets[i].color_b,
                           1.0f);

        for (int t=1;t<=3;t++){
            float inten = 0.6f / (float)t;
            draw_virtual_pixel(rockets[i].x, rockets[i].y + t,
                               rockets[i].color_r/2, rockets[i].color_g/2, rockets[i].color_b/2,
                               inten);
        }
    }

    // 파티클
    for (int i=0;i<MAX_ROCKETS * MAX_PARTICLES_PER_EXPLOSION;i++){
        if (!particles[i].alive) continue;

        float lifeRatio = particles[i].life;
        if (lifeRatio < 0.0f) lifeRatio = 0.0f;
        if (lifeRatio > 1.5f) lifeRatio = 1.5f;

        float baseInt = lifeRatio / 1.5f;

        // 중심
        draw_virtual_pixel(particles[i].x, particles[i].y,
                           particles[i].color_r, particles[i].color_g, particles[i].color_b,
                           baseInt * 1.3f);

        // 주변 빛 번짐
        for (int dy=-1; dy<=1; dy++){
            for (int dx=-1; dx<=1; dx++){
                if (dx==0 && dy==0) continue;
                float d = sqrtf((float)(dx*dx + dy*dy));
                float inten = baseInt * fmaxf(0.0f, 1.0f - (d/1.5f));
                if (inten > 0.01f) {
                    draw_virtual_pixel(particles[i].x+dx, particles[i].y+dy,
                                       particles[i].color_r, particles[i].color_g, particles[i].color_b,
                                       inten * 0.4f);
                }
            }
        }
    }

    // 특수 텍스트
    if (special.active) {
        float life_ratio = special.life / 1.6f;
        if (life_ratio < 0) life_ratio = 0;
        if (life_ratio > 1) life_ratio = 1;

        uint8_t sr = 15, sg = 12, sb = 6;
        draw_text_centered(special.x, special.y,
                           special_texts[special.type],
                           sr, sg, sb,
                           life_ratio);
    }

    // 최종 프레임버퍼 전송
    for (int y=0;y<BUFFER_H;y++){
        for (int x=0;x<BUFFER_W;x++){
            Panel_SetPixel(x,y, fbR[y][x], fbG[y][x], fbB[y][x]);
        }
    }
}
