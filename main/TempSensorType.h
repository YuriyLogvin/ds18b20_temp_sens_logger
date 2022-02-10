#pragma once

#include <ds18x20.h>

typedef struct
{
    ds18x20_addr_t addr;
    float temp;
} TempSensorType;

