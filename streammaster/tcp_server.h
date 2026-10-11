/* SPDX-License-Identifier: GPL-2.0-or-later
 * Optional incoming TCP service. Separate from all outgoing stream slots. */
#ifndef SM_TCP_SERVER_H
#define SM_TCP_SERVER_H
#include <stdint.h>
#define SM_CAP_TCP_SERVER 256U
enum {SM_TCP_LISTEN=60,SM_TCP_ACCEPT,SM_TCP_READ,SM_TCP_WRITE,SM_TCP_CLOSE,SM_TCP_RESET};
typedef struct {uint32_t token,port,peer;} SmTcpListen; /* peer: raw IPv4, zero=any */
typedef struct {uint32_t listener,token;} SmTcpAccept;
typedef struct {uint32_t port,peer;} SmTcpAddress;
#endif
