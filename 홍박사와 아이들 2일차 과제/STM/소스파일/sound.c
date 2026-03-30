// sound.c
#include "stm32f4xx_hal.h"
#include "sound.h"
#include <stdlib.h>

extern TIM_HandleTypeDef htim3;

// 타이머 tick = 1MHz (PSC=83 가정)
#define BUZZER_TIMER_HZ       1000000UL
퍼버벅(폭발 노이즈) 시간
#define FW_RISE_TIME_MS       180U
#define FW_EXPLODE_TIME_MS    150U

// 피융 주파수 범위
#define FW_F_START_HZ         800U    // 시작(저음)
#define FW_F_END_HZ           3500U   // 끝(고음)

// 내부 상태
typedef enum {
    SOUND_IDLE = 0,
    SOUND_RISE,
    SOUND_EXPLODE
} SoundState;

static SoundState s_state = SOUND_IDLE;
static uint32_t   s_phase_start_ms = 0;
static uint32_t   s_last_step_ms   = 0;

// ===== PWM 제어 함수들 =====
static void buzzer_set_freq(uint32_t freq_hz)
{
    if (freq_hz == 0) return;

    uint32_t arr = (BUZZER_TIMER_HZ / freq_hz);
    if (arr == 0) arr = 1;
    arr -= 1;

    if (arr > 0xFFFF) arr = 0xFFFF;

    __HAL_TIM_SET_AUTORELOAD(&htim3, (uint16_t)arr);

    uint32_t ccr = arr / 2;  // 기본 duty 50% 나중에 바꾸면 될듯?
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, (uint16_t)ccr);

    __HAL_TIM_SET_COUNTER(&htim3, 0);
}

static void buzzer_set_duty_percent(uint8_t duty_percent)
{
    if (duty_percent > 100) duty_percent = 100;

    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim3);
    uint32_t ccr = (arr + 1) * duty_percent / 100;

    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, (uint16_t)ccr);
}

static void buzzer_stop(void)
{
    // duty 0%로
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, 0);
}

// ===== 공개 함수 =====

void Sound_Init(void)
{
    // TIM3 PWM은 MX_TIM3_Init에서 이미 설정된 상태라고 가정
    // 여기서는 PWM만 시작 + duty 0으로 mute
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);
    buzzer_stop();

    s_state = SOUND_IDLE;
}

// 폭죽 하나 터질 때마다 호출
void Sound_PlayFirework(void)
{
    // 이미 다른 효과 재생 중이면 덮어쓰기 
    s_state          = SOUND_RISE;
    s_phase_start_ms = HAL_GetTick();
    s_last_step_ms   = s_phase_start_ms;

    // 초기 freq/duty 설정
    buzzer_set_freq(FW_F_START_HZ);
    buzzer_set_duty_percent(40);  // 40% 정도
}

void Sound_Update(uint32_t ms)
{
    switch (s_state) {
    case SOUND_IDLE:
        return;

    case SOUND_RISE:
    {
        uint32_t elapsed = ms - s_phase_start_ms;
        if (ms - s_last_step_ms >= 10U) {
            // 퍼벅
            s_state          = SOUND_EXPLODE;
            s_phase_start_ms = ms;
            s_last_step_ms   = ms;
            return;
        }

        // 10ms마다 주파수 업데이트
        if (elapsed - s_last_step_ms >= 10U) {
            float t = (float)elapsed / (float)FW_RISE_TIME_MS;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;

            float f = (float)FW_F_START_HZ +
                      ((float)FW_F_END_HZ - (float)FW_F_START_HZ) * t;

            buzzer_set_freq((uint32_t)f);
            buzzer_set_duty_percent(45);   // 살짝 더 크게

            s_last_step_ms = ms;
        }
        break;
    }

    case SOUND_EXPLODE:
    {
        uint32_t elapsed = ms - s_phase_start_ms;
        if (elapsed >= FW_EXPLODE_TIME_MS) {
            // 폭발 사운드 종료
            buzzer_stop();
            s_state = SOUND_IDLE;
            return;
        }

        // 8ms마다 랜덤하게 퍼버벅느낌 업뎃
        if (elapsed - s_last_step_ms >= 8U) {
            // 가끔 완전 mute로 구멍 뚫어주기 → 버벅 느낌
            if ((rand() % 5) == 0) {   // 1/5 확률로 잠깐 끔
                buzzer_stop();
            } else {
                // 600Hz ~ 2600Hz 랜덤
                uint32_t f = 600U + (uint32_t)(rand() % 2000U);
                buzzer_set_freq(f);

                // 20% ~ 80% duty 랜덤
                uint8_t duty = 20U + (uint8_t)(rand() % 60U);
                buzzer_set_duty_percent(duty);
            }

            s_last_step_ms = elapsed;
        }
        break;
    }

    default:
        s_state = SOUND_IDLE;
        buzzer_stop();
        break;
    }
}
