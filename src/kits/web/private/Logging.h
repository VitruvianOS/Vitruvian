/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef VOS_LOGGING_H
#define VOS_LOGGING_H


void log_init_from_env();
void log_set_file(const char* path);

void log_error(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void log_warn(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void log_info(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void log_debug(const char* fmt, ...) __attribute__((format(printf, 1, 2)));


#endif // VOS_LOGGING_H
