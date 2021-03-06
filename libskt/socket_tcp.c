/* ************************************************************************
 *       Filename:  socket_tcp.c
 *    Description:  
 *        Version:  1.0
 *        Created:  07/05/2017 05:35:44 PM
 *       Revision:  none
 *       Compiler:  gcc
 *         Author:  YOUR NAME (), 
 *        Company:  
 * ************************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>

#include "socket_tcp.h"



int _inet_addr(struct sockaddr_in *sin, const char *host)
{
        int ret, retry = 0, herrno = 0;
        struct hostent  hostbuf, *result;
        char buf[MAX_BUF_LEN];

retry:
        ret = gethostbyname_r(host, &hostbuf, buf, sizeof(buf),  &result, &herrno);
        if (ret) {
                ret = errno;
                if (ret == EALREADY || ret == EAGAIN) {
                        printf("[warnning] connect addr %s\n", host);
                        ret = EAGAIN;
                        if (retry < 50) {
                                usleep(100 * 1000);
                                retry++;
                                goto retry;
                        } else
                                goto err_ret;
                } else
                        goto err_ret;
        }

        if (result)
                memcpy(&sin->sin_addr, result->h_addr, result->h_length);
        else {
                ret = ENONET;
                printf("[warnning] connect addr %s ret (%u) %s\n", host, ret, strerror(ret));
                goto err_ret;
        }

        return 0;
err_ret:
        return ret;
}

static int __tcp_connect(int s, const struct sockaddr *sin, socklen_t addrlen, int timeout)
{
        int  ret, flags, err;
        socklen_t len;

        flags = fcntl(s, F_GETFL, 0);
        if (flags < 0 ) {
                ret = errno;
                goto err_ret;
        }

        ret = fcntl(s, F_SETFL, flags | O_NONBLOCK);
        if (ret < 0 ) {
                ret = errno;
                goto err_ret;
        }

        ret = connect(s, sin, addrlen);
        if (ret < 0 ) {
                ret = errno;
                if (ret != EINPROGRESS ) {
                        goto err_ret;
                }
        } else
                goto out;

        /*
         *ret = sock_poll_sd(s, timeout * 1000 * 1000, POLLOUT);
         *if (ret) {
         *        goto err_ret;
         *}
         */

        len = sizeof(err);

        ret = getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len);
        if (ret < 0)
                goto err_ret;

        if (err) {
                ret = err;
                goto err_ret;
        }

out:
        ret = fcntl(s, F_SETFL, flags);
        if (ret < 0) {
                ret = errno;
                goto err_ret;
        }

        return 0;
err_ret:
        return ret;
}

static int __tcp_accept(int s, struct sockaddr *sin, socklen_t *addrlen, int timeout)
{
        int  ret, fd;

        (void) timeout;

        fd = accept(s, sin, addrlen);
        if (fd < 0 ) {
                ret = errno;
                goto err_ret;
        }

        return fd;
err_ret:
        return -ret;
}

int tcp_sock_tuning(int sd, int tuning, int nonblock)
{
        int ret, keepalive, nodelay, oob_inline, xmit_buf, flag;
        struct linger lin __attribute__((unused));
        struct timeval tv;
        socklen_t size;

        if (tuning == 0)
                return 0;

        flag = fcntl(sd, F_GETFL);
        if (flag < 0) {
                ret = errno;
                goto err_ret;
        }

        ret = fcntl(sd, F_SETFL, flag | O_CLOEXEC);
        if (ret < 0) {
                ret = errno;
                goto err_ret;
        }

        /*
         * If SO_KEEPALIVE is disabled (default), a TCP connection may remain
         * idle until the connection is released at the protocol layer. If
         * SO_KEEPALIVE is enabled and the connection has been idle for two
         * __hours__, TCP sends a packet to the remote socket, expecting the
         * remote TCP to acknowledge that the connection is still active. If
         * the remote TCP does not respond in a timely manner, TCP continues to
         * send keepalive packets according to the normal retransmission
         * algorithm. If the remote TCP does not respond within a particular
         * time limit, TCP drops the connection. The next socket system call
         * (for example, _recv()) returns an error, and errno is set to
         * ETIMEDOUT.
         */
        keepalive = 1;
        ret = setsockopt(sd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(int));
        if (ret == -1) {
                ret = errno;
                goto err_ret;
        }

        /*
         * If l_onoff is zero (the default action), close() returns immediately,
         * but the system tries to transmit any unsent data and release the
         * protocol connection gracefully. If l_onoff is non-zero and l_linger
         * is zero, close() returns immediately, any unsent data is discarded,
         * and the protocol connection is aborted. If both l_onoff and l_linger
         * are non-zero, close() does not return until the system has tried to
         * transmit all unsent data and release the connection gracefully. In
         * that case, close() can return an error, and errno may be set to
         * ETIMEDOUT, if the system is unable to transmit the data after a
         * protocol-defined time limit. Note that the value of l_linger is
         * treated simply as a boolean; a non-zero value is not interpreted as
         * a time limit( see _XOPEN_SOURCE_EXTENDED Only below). SO_LINGER does
         * not affect the actions taken when the function shutdown() is called.
         */
        if (nonblock == 1){
                flag = fcntl(sd, F_GETFL);
                if (flag == -1) {
                        ret = errno;
                        printf("[error] %d - %s\n", ret, strerror(ret));
                        goto err_ret;
                }

                if ((flag & O_NONBLOCK) == 0) {
                        flag = flag | O_NONBLOCK;
                        ret = fcntl(sd, F_SETFL, flag);
                        if (ret == -1) {
                                ret = errno;
                                printf("[error] %d - %s\n", ret, strerror(ret));
                                goto err_ret;
                        }
                }
        }

        lin.l_onoff = 1;
        lin.l_linger = 15;      /* how many seconds to linger for */
#if 0
        ret = setsockopt(sd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
        if (ret == -1) {
                ret = errno;
                GOTO(err_ret, ret);
        }
#endif

        nodelay = 1;

        ret = setsockopt(sd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(int));
        if (ret == -1) {
                ret = errno;
                if (ret == EOPNOTSUPP) {
                        //nothing todo;
                } else
                        goto err_ret;
        }

        oob_inline = 1;

        ret = setsockopt(sd, SOL_SOCKET, SO_OOBINLINE, &oob_inline,
                         sizeof(int));
        if (ret == -1) {
                ret = errno;
                if (ret == EOPNOTSUPP) {
                        //nothing todo;
                } else
                        goto err_ret;
        }

        tv.tv_sec = 30;
        tv.tv_usec = 0;
        ret = setsockopt(sd, SOL_SOCKET, SO_SNDTIMEO, (void *)&tv,
                         sizeof(struct timeval));
        if (ret == -1) {
                ret = errno;
                printf("[error] %d - %s\n", ret, strerror(ret));
                goto err_ret;
        }

        ret = setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, (void *)&tv,
                         sizeof(struct timeval));
        if (ret == -1) {
                ret = errno;
                printf("[error] %d - %s\n", ret, strerror(ret));
                goto err_ret;
        }

        int wmem_max, rmem_max;
        wmem_max = 1024 * 1024 * 1000;
        rmem_max = wmem_max;
        xmit_buf = wmem_max; 
        ret = setsockopt(sd, SOL_SOCKET, SO_SNDBUF, &xmit_buf, sizeof(int));
        if (ret == -1) {
                ret = errno;
                printf("[error] %d - %s\n", ret, strerror(ret));
                goto err_ret;
        }

        xmit_buf = rmem_max;
        ret = setsockopt(sd, SOL_SOCKET, SO_RCVBUF, &xmit_buf, sizeof(int));
        if (ret == -1) {
                ret = errno;
                printf("[error] %d - %s\n", ret, strerror(ret));
                goto err_ret;
        }

        xmit_buf = 0;
        size = sizeof(int);

        ret = getsockopt(sd, SOL_SOCKET, SO_SNDBUF, &xmit_buf, &size);
        if (ret == -1) {
                ret = errno;
                printf("[error] %d - %s\n", ret, strerror(ret));
                goto err_ret;
        }

        if (xmit_buf != wmem_max * 2) {
                printf("[error] Can't set tcp send buf to %d (got %d)\n",
                       wmem_max, xmit_buf);
        }

        printf("send buf %u\n", xmit_buf);

        xmit_buf = 0;
        size = sizeof(int);

        ret = getsockopt(sd, SOL_SOCKET, SO_RCVBUF, &xmit_buf, &size);
        if (ret == -1) {
                ret = errno;
                printf("[error] %d - %s\n", ret, strerror(ret));
                goto err_ret;
        }

        if (xmit_buf != rmem_max * 2) {
                printf("[error] Can't set tcp recv buf to %d (got %d)\n",
                       rmem_max, xmit_buf);
        }

        printf("recv buf %u\n", xmit_buf);

        return 0;
err_ret:
        return ret;

}



int tcp_sock_bind(int *srv_sd, struct sockaddr_in *sin, int nonblock, int tuning)
{
        int ret, sd, reuse;
        struct protoent ppe, *result;
        char buf[MAX_BUF_LEN];

        /* map protocol name to protocol number */
        ret = getprotobyname_r(NET_TRANSPORT, &ppe, buf, MAX_BUF_LEN, &result);
        if (ret) {
//                ret = ENOENT;
                printf("%s", "can't get \"tcp\" protocol entry\n");
                goto err_ret;
        }

        /* allocate a socket */
        sd = socket(PF_INET, SOCK_STREAM, ppe.p_proto);
        if (sd == -1) {
                ret = errno;
                printf("proto %d name %s\n", ppe.p_proto, ppe.p_name);
                goto err_ret;
        }

        if (tuning) {
                ret = tcp_sock_tuning(sd, 1, nonblock);
                if (ret)
                        goto err_sd;
        }

        reuse = 1;
        ret = setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int));
        if (ret == -1) {
                ret = errno;
                goto err_sd;
        }

        /* bind the socket */
        ret = bind(sd, (struct sockaddr *)sin, sizeof(struct sockaddr));
        if (ret == -1) {
                ret = errno;
                printf("bind %d to %s errno (%d)%s\n", ntohs(sin->sin_port), inet_ntoa(sin->sin_addr), ret, strerror(ret));
                goto err_sd;
        }

        *srv_sd = sd;

        return 0;
err_sd:
        close(sd);
err_ret:
        return ret;
}


int tcp_sock_hostbind(int *srv_sd, const char *host, const char *service, int nonblock)
{
        int ret;
        struct servent  *pse;
        struct sockaddr_in sin;

        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;

        if (host) {
                ret = _inet_addr(&sin, host);
                if (ret)
                        goto err_ret;
        } else
                sin.sin_addr.s_addr = INADDR_ANY;

        if ((pse = getservbyname(service, "tcp")))
                sin.sin_port = pse->s_port;
        else if ((sin.sin_port=htons((unsigned short)atoi(service))) == 0) {
                printf("[error] can't get \"%s\" service entry\n", service);
                ret = ENOENT;
                goto err_ret;
        }

        ret = tcp_sock_bind(srv_sd, &sin, nonblock, 1);
        if (ret)
                goto err_ret;

        return 0;
err_ret:
        return ret;
}


int tcp_sock_listen(int *srv_sd, struct sockaddr_in *sin, int qlen, int nonblock, int tuning)
{
        int ret, sd;

        ret = tcp_sock_bind(&sd, sin, nonblock, tuning);
        if (ret)
                goto err_ret;


        ret = listen(sd, qlen);
        if (ret == -1) {
                ret = errno;
                goto err_sd;
        }

        *srv_sd = sd;

        return 0;
err_sd:
        close(sd);
err_ret:
        return ret;
}


int tcp_sock_hostlisten(int *srv_sd, const char *host, const char *service,
                        int qlen, int nonblock, int tuning)
{
        int ret;
        struct servent  *pse;
        struct sockaddr_in sin;

        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;

        if (host) {
                ret = _inet_addr(&sin, host);
                if (ret)
                        goto err_ret;
        } else
                sin.sin_addr.s_addr = INADDR_ANY;

        if ((pse = getservbyname(service, "tcp")))
                sin.sin_port = pse->s_port;
        else if ((sin.sin_port=htons((unsigned short)atoi(service))) == 0) {
                printf("[error] can't get \"%s\" service entry\n", service);
                ret = ENOENT;
                goto err_ret;
        }

        ret = tcp_sock_listen(srv_sd, &sin, qlen, nonblock, tuning);
        if (ret) {
                goto err_ret;
        }

        return 0;
err_ret:
        return ret;
}


int tcp_sock_addrlisten(int *srv_sd, uint32_t addr, uint32_t port, int qlen, int nonblock)
{
        int ret;
        struct sockaddr_in sin;

        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;

        if (addr != 0)
                sin.sin_addr.s_addr = addr;
        else
                sin.sin_addr.s_addr = INADDR_ANY;

        sin.sin_port = htons(port);

        ret = tcp_sock_listen(srv_sd, &sin, qlen, nonblock, 1);
        if (ret)
                goto err_ret;

        return 0;
err_ret:
        return ret;
}

/**
 * 随机返回一个port
 * */
int tcp_sock_portlisten(int *srv_sd, uint32_t addr, uint32_t *_port, int qlen, int nonblock)
{
        int ret;
        struct sockaddr_in sin;
        uint16_t port = 0;

        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;

        if (addr != 0)
                sin.sin_addr.s_addr = addr;
        else
                sin.sin_addr.s_addr = INADDR_ANY;

        while (1) {
                port = (uint16_t)(NET_SERVICE_BASE
                                  + (random() % NET_SERVICE_RANGE));

                sin.sin_port = htons(port);

                ret = tcp_sock_listen(srv_sd, &sin, qlen, nonblock, 1);
                if (ret) {
                        if (ret == EADDRINUSE) {
                                printf("port (%u + %u) %s\n", NET_SERVICE_BASE,
                                     port - NET_SERVICE_BASE, strerror(ret));
                                continue;
                        }
                        goto err_ret;

                } else
                        break;
        }

        *_port = port;

        return 0;
err_ret:
        return ret;
}


int tcp_sock_accept(net_handle_t *nh, int srv_sd, int tuning, int nonblock)
{
        int ret, sd;
        struct sockaddr_in sin;
        socklen_t alen;

        memset(&sin, 0, sizeof(sin));
        alen = sizeof(struct sockaddr_in);

        sd = __tcp_accept(srv_sd, (struct sockaddr *)&sin, &alen, 60 / 2);
        if (sd < 0) {
                ret = -sd;
                printf("[error] srv_sd %d, %u, %s\n", srv_sd, ret, strerror(ret));
                goto err_ret;
        }

        ret = tcp_sock_tuning(sd, tuning, nonblock);
        if (ret)
                goto err_ret;

        memset(nh, 0x0, sizeof(*nh));
        nh->type = NET_HANDLE_TRANSIENT;
        nh->u.sd.sd = sd;
        nh->u.sd.addr = sin.sin_addr.s_addr;

        return 0;
err_sd:
        close(sd);
err_ret:
        return ret;
}

int tcp_sock_accept_sd(int *_sd, int srv_sd, int tuning, int nonblock)
{
        int ret, sd;
        struct sockaddr_in sin;
        socklen_t alen;

        memset(&sin, 0, sizeof(sin));
        alen = sizeof(struct sockaddr_in);

        sd = __tcp_accept(srv_sd, (struct sockaddr *)&sin, &alen, 60 / 2);
        if (sd < 0) {
                ret = -sd;
                printf("[error] srv_sd %d\n", srv_sd);
                goto err_ret;
        }

        ret = tcp_sock_tuning(sd, tuning, nonblock);
        if (ret)
                goto err_ret;

        *_sd = sd;

        return 0;
err_sd:
        close(sd);
err_ret:
        return ret;
}


int tcp_sock_connect(net_handle_t *nh, struct sockaddr_in *sin, int nonblock, int timeout, int tuning)
{
        int ret, sd;

        sd = socket(PF_INET, SOCK_STREAM, 0);
        if (sd == -1) {
                ret = errno;
                goto err_ret;
        }

        //ret = connect(sd, (struct sockaddr*)sin, sizeof(struct sockaddr));
        ret = __tcp_connect(sd, (struct sockaddr*)sin, sizeof(struct sockaddr),
                            timeout);
        if (ret) {
                goto err_sd;
        }

        if (tuning) {
                ret = tcp_sock_tuning(sd, 1, nonblock);
                if (ret) 
                        goto err_sd;
        }

        printf("[debug] new sock %d connected\n", sd);
        nh->u.sd.sd = sd;
        nh->u.sd.addr = sin->sin_addr.s_addr;
        //sock->proto = ng.op;

        return 0;
err_sd:
        close(sd);
err_ret:
        return ret;
}


int tcp_sock_hostconnect(net_handle_t *nh, const char *host,
                         const char *service, int nonblock, int timeout, int tuning)
{
        int ret;
        struct servent  *pse;
        struct sockaddr_in sin;

        memset(&sin, 0, sizeof(struct sockaddr_in));
        sin.sin_family = AF_INET;

        if ((pse = getservbyname(service, "tcp")))
                sin.sin_port = pse->s_port;
        else if ((sin.sin_port = htons(atoi(service))) == 0) {
                ret = ENOENT;
                printf("[error] get port from service (%s)\n", service);
                assert(0);
                goto err_ret;
        }

        ret = _inet_addr(&sin, host);
        if (ret)
                goto err_ret;

        printf("[info] host %s service %s --> ip %s port %d\n",
             host, service, 
             inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));

        ret = tcp_sock_connect(nh, &sin, nonblock, timeout, tuning);
        if (ret) {
                goto err_ret;
        }

        return 0;
err_ret:
        return ret;
}


int tcp_sock_close(int sd)
{
        int ret;
        while (1) {
                ret = close(sd);
                if (ret == -1) {
                        ret = errno;

                        if(ret == EINTR)
                                continue;
                        goto err_ret;
                } else
                        break;
        }
        return 0;
err_ret:
        return ret;
}


