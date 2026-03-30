
// ======================= panel.c =======================
#include "panel.h"

extern TIM_HandleTypeDef htim2;

static uint8_t planeR[BIT_DEPTH][PANEL_HEIGHT][PANEL_WIDTH];
static uint8_t planeG[BIT_DEPTH][PANEL_HEIGHT][PANEL_WIDTH];
static uint8_t planeB[BIT_DEPTH][PANEL_HEIGHT][PANEL_WIDTH];

static uint8_t curBit = 0;
static uint8_t curRow = 0;
static uint16_t ticksRemain = 0;
static uint8_t needLoad = 1;

void Panel_SetPixel(uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b)
{
    if (x >= PANEL_WIDTH || y >= PANEL_HEIGHT) return;

    for (uint8_t bit = 0; bit < BIT_DEPTH; bit++) {
        planeR[bit][y][x] = (r >> bit) & 1;
        planeG[bit][y][x] = (g >> bit) & 1;
        planeB[bit][y][x] = (b >> bit) & 1;
    }
}


void Panel_GPIO_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = PIN_R1|PIN_G1|PIN_B1|PIN_R2|PIN_G2|PIN_B2|PIN_CLK|PIN_LAT;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(PANEL_PORT_DATA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = PIN_A|PIN_B|PIN_C|PIN_D|PIN_OE;
    HAL_GPIO_Init(PANEL_PORT_CTRL, &GPIO_InitStruct);

    HAL_GPIO_WritePin(PANEL_PORT_CTRL, PIN_OE, GPIO_PIN_SET);
}

static inline void Panel_SetRow(uint8_t row)
{
    uint32_t odr = PANEL_PORT_CTRL->ODR & ~ROW_MASK;
    if (row & 1) odr |= PIN_A;
    if (row & 2) odr |= PIN_B;
    if (row & 4) odr |= PIN_C;
    if (row & 8) odr |= PIN_D;
    PANEL_PORT_CTRL->ODR = odr;
}

static inline void OE_On(void) { PANEL_PORT_CTRL->BSRR = (uint32_t)PIN_OE << 16; }
static inline void OE_Off(void) { PANEL_PORT_CTRL->BSRR = PIN_OE; }
static inline void CLK_Pulse(void)
{
    PANEL_PORT_DATA->BSRR = PIN_CLK;
    PANEL_PORT_DATA->BSRR = (uint32_t)PIN_CLK << 16;
}
static inline void LAT_Pulse(void)
{
    PANEL_PORT_DATA->BSRR = PIN_LAT;
    PANEL_PORT_DATA->BSRR = (uint32_t)PIN_LAT << 16;
}

static void Shift_Row(uint8_t bit, uint8_t rowGroup)
{
    uint8_t y1 = rowGroup;
    uint8_t y2 = rowGroup + PANEL_ROW_GROUPS;

    for (uint8_t x = 0; x < PANEL_WIDTH; x++) {
        uint32_t odr = PANEL_PORT_DATA->ODR & ~DATA_MASK;

        if (planeR[bit][y1][x]) odr |= PIN_R1;
        if (planeG[bit][y1][x]) odr |= PIN_G1;
        if (planeB[bit][y1][x]) odr |= PIN_B1;

        if (planeR[bit][y2][x]) odr |= PIN_R2;
        if (planeG[bit][y2][x]) odr |= PIN_G2;
        if (planeB[bit][y2][x]) odr |= PIN_B2;

        PANEL_PORT_DATA->ODR = odr;
        CLK_Pulse();
    }
}

void Panel_StartRefresh(void)
{
    curBit = 0;
    curRow = 0;
    ticksRemain = 0;
    needLoad = 1;

    HAL_TIM_Base_Start_IT(&htim2);   // TIM2 인터럽트 시작
}

void Panel_ScanISR(void)
{
    const uint16_t baseTicks = 1;  // 1,2,3 정도로 조절 가능 (밝기/프레임레이트 튜닝용)

    if (needLoad) {
        // 이전 라인 끄고 → 새 라인 데이터 로드
        OE_Off();
        Panel_SetRow(curRow);
        Shift_Row(curBit, curRow);
        LAT_Pulse();
        OE_On();   // 출력 켜기

        ticksRemain = (uint16_t)(baseTicks << curBit);  // 1,2,4,8 배 가중치
        needLoad = 0;
        return;
    }

    // 이 상태를 더 유지할지 확인
    if (ticksRemain > 0) {
        if (--ticksRemain == 0) {
            // 이 상태 끝 → 다음 상태로 넘어갈 준비
            needLoad = 1;

            // 다음 row / bit 결정
            curRow++;
            if (curRow >= PANEL_ROW_GROUPS) {
                curRow = 0;
                curBit++;
                if (curBit >= BIT_DEPTH) {
                    curBit = 0;   // 한 프레임 끝
                }
            }
        }
    }
}
