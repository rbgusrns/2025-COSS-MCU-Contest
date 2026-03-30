#ifndef __FIREWORKS_H
#define __FIREWORKS_H

#include <stdint.h>
#include <stdbool.h>

void Fireworks_Init(void);
void Fireworks_Update(uint32_t ms);
void Fireworks_Render(void);

// 버튼 눌러서 불꽃 발사
void Fireworks_TriggerSpawn(void);

// 자동 모드 토글
void Fireworks_ToggleAuto(void);

#endif
