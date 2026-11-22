/* SPDX-License-Identifier: MIT */
#pragma once
/* No test in this suite may open or shut a real socket. */
#define SHUT_RDWR 2
#define shutdown test_shutdown
int test_shutdown(int socket, int how);
