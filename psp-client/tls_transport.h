#ifndef PSP_STREAMER_TLS_TRANSPORT_H
#define PSP_STREAMER_TLS_TRANSPORT_H
/* Socket owner opens/reads/frees TLS. Other threads may shutdown, never free. */
int tls_init(void);
int tls_open(int fd, const char *host, int port, volatile int *running, int timeout_ms);
int tls_recv(int fd, void *buffer, int size, int timeout_ms);
int tls_send(int fd, const void *buffer, int size, volatile int *running, int timeout_ms);
int tls_close(int fd);
/* Sticky until dismissed: 1 first certificate, 2 changed, 3 storage error. */
int tls_notice(void);
void tls_notice_clear(void);
#endif
