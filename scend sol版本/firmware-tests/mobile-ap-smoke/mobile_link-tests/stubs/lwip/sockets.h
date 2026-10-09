#pragma once
#include "lwip/err.h"
int close(int);
#define SHUT_RDWR 2
int shutdown(int,int);
