#include "traffic.h"
#include "panel.h"          // PANEL_WIDTH, PANEL_HEIGHT
#include "stm32f4xx_hal.h"  // HAL_GetTick()
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// 전체 버퍼 크기 (128x32)
#define W PANEL_WIDTH
#define H PANEL_HEIGHT


// ==== 차량 스폰 간격  ====
#define CAR_SPAWN_MIN_INTERVAL_MS  4500
#define CAR_SPAWN_MAX_INTERVAL_MS  6000

static uint32_t next_spawn_interval_ms = 5000;


// ==== 패널 구분 및 도로 위치 ====
// 하단 패널의 X좌표 시작점 (0~63은 상단, 64~127은 하단)
#define BOTTOM_PANEL_START_X  64

// 도로 Y 좌표 계산
#define PIXELS_FROM_BOTTOM    24
#define ROAD_Y                (31 - PIXELS_FROM_BOTTOM)

// ==== 자동차 설정 ====
#define MAX_CARS              8
#define CAR_LEN_PIXELS        2
#define CAR_STEP_INTERVAL_MS  80

// 4비트 RGB (빨간색)
#define CAR_COLOR_R  15
#define CAR_COLOR_G  0
#define CAR_COLOR_B  0

typedef struct {
    int  x;        // 128x32 버퍼 기준 X좌표
    int  y;
    bool active;
} Car;

static Car cars[MAX_CARS];
static uint32_t last_step_ms  = 0;
static uint32_t last_spawn_ms = 0;

// 오른쪽 끝(하단 패널의 입구) 근처에 차가 있는지 확인
static bool has_car_near_entry(void)
{
    for (int i = 0; i < MAX_CARS; i++) {
        if (!cars[i].active) continue;
        // 화면 오른쪽 끝(128)에서 6픽셀 이내에 차가 있으면 스폰 금지
        if (cars[i].x > W - 6) {
            return true;
        }
    }
    return false;
}

static void spawn_car(void)
{
    int idx = -1;
    for (int i = 0; i < MAX_CARS; i++) {
        if (!cars[i].active) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return;

    if (has_car_near_entry()) return;

    // 하단 패널의 오른쪽 끝(128)에서 시작
    cars[idx].x = W;
    cars[idx].y = ROAD_Y;
    cars[idx].active = true;
}

void Traffic_Init(void)
{
    memset(cars, 0, sizeof(cars));

    uint32_t now = HAL_GetTick();
    last_step_ms  = now;
    last_spawn_ms = now;

    next_spawn_interval_ms =
        CAR_SPAWN_MIN_INTERVAL_MS +
        (rand() % (CAR_SPAWN_MAX_INTERVAL_MS - CAR_SPAWN_MIN_INTERVAL_MS + 1));
}

void Traffic_Update(void)
{
    uint32_t now = HAL_GetTick();

    // 이동 로직
    if ((now - last_step_ms) >= CAR_STEP_INTERVAL_MS) {
        last_step_ms = now;

        for (int i = 0; i < MAX_CARS; i++) {
            if (!cars[i].active) continue;

            cars[i].x -= 1; // 왼쪽으로 이동

            // 차가 하단 패널 영역(X >= 64)을 벗어나 상단 패널 영역(X < 64)으로 진입하면
            // 완전히 사라지게 처리 (꼬리까지 고려하여 완전히 넘어가면 비활성화)
            if (cars[i].x + CAR_LEN_PIXELS < BOTTOM_PANEL_START_X) {
                cars[i].active = false;
            }
        }
    }

    // 생성 로직 (랜덤 간격)
    if ((now - last_spawn_ms) >= next_spawn_interval_ms) {
        last_spawn_ms = now;
        spawn_car();

        // 다음 출현 간격 랜덤 갱신
        next_spawn_interval_ms =
            CAR_SPAWN_MIN_INTERVAL_MS +
            (rand() % (CAR_SPAWN_MAX_INTERVAL_MS - CAR_SPAWN_MIN_INTERVAL_MS + 1));
    }
}

void Traffic_OverlayPixel(int x, int y, uint8_t *r, uint8_t *g, uint8_t *b)
{
    // 1. 도로 라인이 아니면 무시
    if (y != ROAD_Y) return;

    // 2. 상단 패널 영역(0~63)이면 무시 (하단 패널인 64~127만 그리기)
    if (x < BOTTOM_PANEL_START_X) return;

    for (int i = 0; i < MAX_CARS; i++) {
        if (!cars[i].active) continue;

        int cx = cars[i].x;
        int cx_end = cx + CAR_LEN_PIXELS;

        if (x >= cx && x < cx_end) {
            // 자동차 그리기
            *r = CAR_COLOR_R;
            *g = CAR_COLOR_G;
            *b = CAR_COLOR_B;
            return;
        }
    }
}
