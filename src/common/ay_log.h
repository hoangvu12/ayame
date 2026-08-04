#pragma once

#include <stdarg.h>

void ay_log_open(void);
void ay_log(const char *fmt, ...);
void ay_logv(const char *fmt, va_list ap);
