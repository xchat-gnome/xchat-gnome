/* include stuff for internet */

#ifndef WIN32

#ifdef WANTSOCKET
#include <sys/socket.h>
#include <sys/types.h>
#endif
#ifdef WANTARPA
#include <arpa/inet.h>
#include <netinet/in.h>
#endif
#ifdef WANTDNS
#include <netdb.h>
/* OpenBSD's netdb.h does not define AI_ADDRCONFIG */
#ifndef AI_ADDRCONFIG
#define AI_ADDRCONFIG 0
#endif
#endif
#define closesocket close
#define set_blocking(sok) fcntl(sok, F_SETFL, 0)
#define set_nonblocking(sok) fcntl(sok, F_SETFL, O_NONBLOCK)
#define would_block() (errno == EAGAIN || errno == EWOULDBLOCK)
#define sock_error() (errno)

#else

#include <winsock2.h>
#include <ws2tcpip.h>

#define set_blocking(sok)                                                                          \
        {                                                                                          \
                unsigned long zero = 0;                                                            \
                ioctlsocket(sok, FIONBIO, &zero);                                                  \
        }
#define set_nonblocking(sok)                                                                       \
        {                                                                                          \
                unsigned long one = 1;                                                             \
                ioctlsocket(sok, FIONBIO, &one);                                                   \
        }
#define would_block() (WSAGetLastError() == WSAEWOULDBLOCK)
#define sock_error WSAGetLastError

#endif
