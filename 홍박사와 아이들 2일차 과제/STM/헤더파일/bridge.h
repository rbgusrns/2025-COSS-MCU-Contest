#ifndef __BRIDGE_H
#define __BRIDGE_H

#include <stdint.h>

void Bridge_Init(void);

// (x,y) 픽셀의 배경색 가져오기: 0~7 범위
void Bridge_GetPixel(int x, int y, uint8_t *r, uint8_t *g, uint8_t *b);

#endif
