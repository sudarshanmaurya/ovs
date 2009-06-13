/*
 * Copyright (c) 2008, 2009 Nicira Networks.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <config.h>
#include "vconn.h"
#include <errno.h>
#include <inttypes.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "packets.h"
#include "socket-util.h"
#include "util.h"
#include "openflow/openflow.h"
#include "vconn-provider.h"
#include "vconn-stream.h"

#include "vlog.h"
#define THIS_MODULE VLM_vconn_tcp

/* Active TCP. */

static int
new_tcp_vconn(const char *name, int fd, int connect_status,
              const struct sockaddr_in *sin, struct vconn **vconnp)
{
    int on = 1;
    int retval;

    retval = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
    if (retval) {
        VLOG_ERR("%s: setsockopt(TCP_NODELAY): %s", name, strerror(errno));
        close(fd);
        return errno;
    }

    return new_stream_vconn(name, fd, connect_status, sin->sin_addr.s_addr,
                            true, vconnp);
}

static int
tcp_open(const char *name, char *suffix, struct vconn **vconnp)
{
    struct sockaddr_in sin;
    int fd, error;

    error = tcp_open_active(suffix, OFP_TCP_PORT, NULL, &fd);
    if (fd >= 0) {
        return new_tcp_vconn(name, fd, error, &sin, vconnp);
    } else {
        VLOG_ERR("%s: connect: %s", name, strerror(error));
        return error;
    }
}

struct vconn_class tcp_vconn_class = {
    "tcp",                      /* name */
    tcp_open,                   /* open */
    NULL,                       /* close */
    NULL,                       /* connect */
    NULL,                       /* recv */
    NULL,                       /* send */
    NULL,                       /* wait */
};

/* Passive TCP. */

static int ptcp_accept(int fd, const struct sockaddr *sa, size_t sa_len,
                       struct vconn **vconnp);

static int
ptcp_open(const char *name UNUSED, char *suffix, struct pvconn **pvconnp)
{
    int fd;

    fd = tcp_open_passive(suffix, OFP_TCP_PORT);
    if (fd < 0) {
        return -fd;
    } else {
        return new_pstream_pvconn("ptcp", fd, ptcp_accept, pvconnp);
    }
}

static int
ptcp_accept(int fd, const struct sockaddr *sa, size_t sa_len,
            struct vconn **vconnp)
{
    const struct sockaddr_in *sin = (const struct sockaddr_in *) sa;
    char name[128];

    if (sa_len == sizeof(struct sockaddr_in) && sin->sin_family == AF_INET) {
        sprintf(name, "tcp:"IP_FMT, IP_ARGS(&sin->sin_addr));
        if (sin->sin_port != htons(OFP_TCP_PORT)) {
            sprintf(strchr(name, '\0'), ":%"PRIu16, ntohs(sin->sin_port));
        }
    } else {
        strcpy(name, "tcp");
    }
    return new_tcp_vconn(name, fd, 0, sin, vconnp);
}

struct pvconn_class ptcp_pvconn_class = {
    "ptcp",
    ptcp_open,
    NULL,
    NULL,
    NULL
};

