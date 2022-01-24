#pragma once

#include <stdint.h>
#include <stdbool.h>

extern int init_ip_allow_list (void);
extern void close_ip_allow_list (void);
extern bool is_allow (const uint32_t ip);
extern void update_accesslist (const int signo);
