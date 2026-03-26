#pragma once

#include <cstdio>

#define LOG_ERR(tag, fmt, ...) std::fprintf(stderr, "[ERR] %s: " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_INF(tag, fmt, ...) std::fprintf(stderr, "[INF] %s: " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_DBG(tag, fmt, ...) std::fprintf(stderr, "[DBG] %s: " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_WRN(tag, fmt, ...) std::fprintf(stderr, "[WRN] %s: " fmt "\n", tag, ##__VA_ARGS__)
