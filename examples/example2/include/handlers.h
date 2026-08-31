#pragma once

#include <stddef.h>

void dispatch_buffer(const unsigned char *data, size_t len);
void handle_gzip(const unsigned char *data, size_t len);
