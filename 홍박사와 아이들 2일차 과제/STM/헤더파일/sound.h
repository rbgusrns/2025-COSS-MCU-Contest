// sound.h
#ifndef __SOUND_H
#define __SOUND_H

#include <stdint.h>

void Sound_Init(void);
void Sound_PlayFirework(void);   // 피융~퍼버벅 효과 시작
void Sound_Update(uint32_t ms);  
#endif
