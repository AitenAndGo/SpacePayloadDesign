/*
 * Maxon motor control – stub implementations (no hardware yet).
 */

#include "maxon.h"

void maxon_init(void)
{
}

void maxon_enable(void)
{
}

void maxon_disable(void)
{
}

void maxon_set_velocity(int32_t velocity_cps)
{
    (void)velocity_cps;
}

void maxon_set_position(int32_t position_counts)
{
    (void)position_counts;
}

void maxon_stop(void)
{
}

void maxon_set_velocity_axis(uint8_t axis, int32_t velocity_cps)
{
    (void)axis;
    (void)velocity_cps;
}

void maxon_set_position_axis(uint8_t axis, int32_t position_counts)
{
    (void)axis;
    (void)position_counts;
}
