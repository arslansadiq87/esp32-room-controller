#pragma once
#include <winsock2.h>
inline int settimeofday(const timeval*,void*) { return 0; }
