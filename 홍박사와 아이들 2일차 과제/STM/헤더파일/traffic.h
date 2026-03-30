// traffic.h
#ifndef TRAFFIC_H
#define TRAFFIC_H

#include <stdint.h>

void Traffic_Init(void);
void Traffic_Update(void);
void Traffic_OverlayPixel(int x, int y, uint8_t *r, uint8_t *g, uint8_t *b);

#endif
