/*
 * This file is part of RTRlib.
 *
 * This file is subject to the terms and conditions of the MIT license.
 * See the file LICENSE in the top level directory for more details.
 *
 * Website: http://rtrlib.realmv6.org/
 */

#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>

#ifdef HAVE_NETNS
#undef _GNU_SOURCE
#define _GNU_SOURCE
#include <sched.h>
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <asm-generic/unistd.h>
#include <sys/syscall.h>

#include "rtrlib/lib/vrf.h"
#include "rtrlib/rtrlib_export_private.h"
#include "rtrlib/lib/log_private.h"

#define VRF_NETNS_DEFAULT_NAME    "/proc/self/ns/net"
#define VRF_NETNS_RUNDIR_NAME    "/var/run/netns/"

static int vrf_netns_default_fd;
static int vrf_netns_current_fd;
static int vrf_netns_not_available;
static int vrf_netns_activate;

#ifndef CLONE_NEWNET
#define CLONE_NEWNET 0x40000000
/* New network namespace (lo, device, names sockets, etc) */
#endif

#ifndef HAVE_SETNS
static inline int setns(int fd, int nstype)
{
#ifdef __NR_setns
	return syscall(__NR_setns, fd, nstype);
#else
	errno = EINVAL;
	return -1;
#endif
}
#endif /* !HAVE_SETNS */


static char *vrf_netns_pathname(const char *netns_name)
{
	static char pathname[PATH_MAX];
	char *result;

	if (netns_name[0] == '/') /* absolute pathname */
		result = realpath(netns_name, pathname);
	else {
		/* relevant pathname */
		char tmp_name[PATH_MAX];

		snprintf(tmp_name, PATH_MAX, "%s%s",
                         VRF_NETNS_RUNDIR_NAME, netns_name);
		result = realpath(tmp_name, pathname);
	}

	if (!result)
		return NULL;
	return pathname;
}

RTRLIB_EXPORT void vrf_netns_init(void)
{
  vrf_netns_default_fd = open(VRF_NETNS_DEFAULT_NAME, O_RDONLY);
  if (vrf_netns_default_fd < 0)
    vrf_netns_not_available = 1;
  return;
}

RTRLIB_EXPORT void vrf_netns_activate_support(void)
{
  vrf_netns_activate = 1;
}

RTRLIB_EXPORT int vrf_netns_switch_to(const char *name)
{
  int ret, fd;
  char *path;

  /* ignore errors */
  if (!vrf_netns_activate || vrf_netns_not_available
      || !name || vrf_netns_default_fd == -1)
    return 0;
  path = vrf_netns_pathname(name);
  if (!path)
    return -1;
  fd = open(path, O_RDONLY);
  if (fd == -1) {
    errno = EINVAL;
    return -1;
  }
  ret = setns(fd, CLONE_NEWNET);
  if (ret < 0)
    return ret;
  vrf_netns_current_fd = fd;
  close(fd);
  return ret;
}

RTRLIB_EXPORT int vrf_netns_api_usable(const char *name)
{
  char path[255];
  int fd;

  /* if netns is not enabled or vrf is not mentioned, return true */
  if (!vrf_netns_activate || vrf_netns_not_available || !name)
    return 1;
  snprintf(path, sizeof(path), "%s%s",
           VRF_NETNS_RUNDIR_NAME, name);
  fd = open(path, O_RDONLY);
  if (fd < 0)
    return -1;
  close(fd);
  return 1;
}

RTRLIB_EXPORT int vrf_netns_switchback(void)
{
  int ret = 0;

  if (!vrf_netns_activate || vrf_netns_not_available)
    return ret;
  if (vrf_netns_current_fd != -1 && vrf_netns_default_fd != -1) {
    ret = setns(vrf_netns_default_fd, CLONE_NEWNET);
    vrf_netns_current_fd = -1;
  }
  return ret;
}

RTRLIB_EXPORT int vrf_netns_socket(int domain, int type, int protocol, const char *name)
{
  int ret, save_errno;

  if (!vrf_netns_activate || vrf_netns_not_available || !name)
    return socket(domain, type, protocol);

  ret = vrf_netns_switch_to(name);
  if (ret < 0)
    return ret;
  ret = socket(domain, type, protocol);
  if (ret < 0)
    return ret;
  save_errno = errno;
  vrf_netns_switchback();
  errno = save_errno;

  return ret;
}
