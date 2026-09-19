#pragma once
#include <winsock2.h>

// Narrow socket seam for deterministic timeout/partial-write regression tests.
struct VizSocketOps
{
    decltype(&::connect) connect = &::connect;
    decltype(&::send) send = &::send;
    decltype(&::select) select = &::select;
};
