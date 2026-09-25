#ifndef AMBIENT_ORBS_H
#define AMBIENT_ORBS_H

#include <gsKit.h>
#include <stdint.h>

void ambient_orbs_draw_background(GSGLOBAL *gs);
void ambient_orbs_draw(GSGLOBAL *gs, uint32_t elapsed_ms,
                       int center_x, int center_y,
                       int radius_x, int radius_y, int glow_scale);

#endif
