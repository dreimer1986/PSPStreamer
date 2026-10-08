/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <pspnet_inet.h>
#include "tls_transport.h"
#include "../streammaster/protocol.h"
#include "../streammaster/trace_protocol.h"
int stm_trace_snapshot(SmTrace *out);
int stm_init(int enabled,const char *host,int port,int https);
int stm_enabled(void);
void stm_app_rumble(unsigned small,unsigned large);
void stm_diagnostic_enable(int enabled);
int stm_diagnostic_snapshot(char *line,unsigned size,int buffers);
int stm_download_snapshot(int fd,char *line,unsigned size);
int stm_download_failure(int fd,unsigned row,char *line,unsigned size);
void stm_tuning(unsigned kib,unsigned depth);
int stm_usb_metrics(SmUsbMetrics *out);
int stm_network_diagnostic(SmNetDiag *out);
void stm_server(const char *host,int port,int https);
int stm_rpc(unsigned op,const void *data,unsigned size,void *reply,unsigned capacity,unsigned *length,volatile int *running);
int stm_driver_start(int force,volatile int *running);
void stm_driver_cancel(void);
void stm_driver_stop(void);
int stm_associate(volatile int *running,int force);
const char *stm_stage(void);
int stm_socket(int domain,int type,int protocol);
int stm_connect(int fd,const struct sockaddr *address,socklen_t size);
int stm_poll(struct SceNetInetPollfd *fds,size_t count,int timeout);
int stm_setsockopt(int fd,int level,int option,const void *value,socklen_t size);
int stm_getsockopt(int fd,int level,int option,void *value,socklen_t *size);
size_t stm_send(int fd,const void *data,size_t size,int flags);
size_t stm_recv(int fd,void *data,size_t size,int flags);
int stm_errno(void);
void stm_thread_finished(void);
int stm_apstate(int *state);
int stm_tls_open(int fd,const char *host,int port,volatile int *running,int timeout);
int stm_tls_send(int fd,const void *data,int size,volatile int *running,int timeout);
int stm_tls_recv(int fd,void *data,int size,int timeout);
int stm_close(int fd);
