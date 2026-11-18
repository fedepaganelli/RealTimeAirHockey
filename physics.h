#pragma once
#include <time.h>

void add_timespec(struct timespec* ts,long nanoseconds);
void* physics_task(void* arg);

