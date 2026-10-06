/* Copyright (C) 2026 Mihawk-99 */
/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef PW_WINE_PROCESS_IO_H
#define PW_WINE_PROCESS_IO_H
#include <stddef.h>
#include <sys/types.h>
int ps5_gpu_packet_socketpair(int pair[2], size_t packet_size);
int ps5_gpu_packet_send(int fd, const void *data, size_t size, const int *fds, unsigned count);
ssize_t ps5_gpu_packet_recv(int fd, void *data, size_t size, int fds[2], unsigned *count);
#endif
