/*
 * Copyright 2019 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>

#include "os_socket.h"

#if defined(__linux__)

#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int
os_socket_listen_abstract(const char *path, int count)
{
   int s = socket(AF_UNIX, SOCK_STREAM, 0);
   if (s < 0)
      return -1;

   struct sockaddr_un addr;
   memset(&addr, 0, sizeof(addr));
   addr.sun_family = AF_UNIX;
   strncpy(addr.sun_path + 1, path, sizeof(addr.sun_path) - 2);

   /* Create an abstract socket */
   int ret = bind(s, (struct sockaddr*)&addr,
                  offsetof(struct sockaddr_un, sun_path) +
                  strlen(path) + 1);
   if (ret < 0)
      return -1;

   listen(s, count);

   return s;
}

int
os_socket_accept(int s)
{
   return accept(s, NULL, NULL);
}

ssize_t
os_socket_recv(int socket, void *buffer, size_t length, int flags)
{
   return recv(socket, buffer, length, flags);
}

ssize_t
os_socket_send(int socket, const void *buffer, size_t length, int flags)
{
   return send(socket, buffer, length, flags);
}

void
os_socket_block(int s, bool block)
{
   int old = fcntl(s, F_GETFL, 0);
   if (old == -1)
      return;

   /* TODO obey block */
   if (block)
      fcntl(s, F_SETFL, old & ~O_NONBLOCK);
   else
      fcntl(s, F_SETFL, old | O_NONBLOCK);
}

void
os_socket_close(int s)
{
   close(s);
}

#else

int
os_socket_listen_abstract(const char *path, int count)
{
   errno = -ENOSYS;
   return -1;
}

int
os_socket_accept(int s)
{
   errno = -ENOSYS;
   return -1;
}

ssize_t
os_socket_recv(int socket, void *buffer, size_t length, int flags)
{
   errno = -ENOSYS;
   return -1;
}

ssize_t
os_socket_send(int socket, const void *buffer, size_t length, int flags)
{
   errno = -ENOSYS;
   return -1;
}

void
os_socket_block(int s, bool block)
{
}

void
os_socket_close(int s)
{
}

#endif
