/* App-only adapter. Native descriptors retain their original implementations.
 * Included before socket diagnostics, not in the transport implementation. */
#include "streammaster_transport.h"
#define sceNetInetSocket stm_socket
#define sceNetInetConnect stm_connect
#define sceNetInetPoll stm_poll
#define sceNetInetSetsockopt stm_setsockopt
#define sceNetInetGetsockopt stm_getsockopt
#define sceNetInetSend stm_send
#define sceNetInetRecv stm_recv
#define sceNetInetGetErrno stm_errno
#define sceNetApctlGetState stm_apstate
#define tls_open stm_tls_open
#define tls_send stm_tls_send
#define tls_recv stm_tls_recv
#define tls_close stm_close
