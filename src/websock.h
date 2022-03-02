#ifndef WEBSOCK_H
#define WEBSOCK_H 1

#define WS_KEY "dGhlIHNhbXBsZSBub25jZQ=="
#define WS_ACC "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="

#define WS_CONTINUE	0x0
#define WS_TEXT		0x1
#define WS_BIN		0x2
#define WS_CLOSE	0x8
#define WS_PING		0x9
#define WS_PONG		0xA

#define WS_OP_MASK 0x0F

#define WS_FIN 0x80

#include <lt/fwd.h>
#include <lt/lt.h>

void ws_send_frame_start(lt_socket_t* sock, u8 op, usz len);
void ws_send_text(lt_socket_t* sock, lstr_t data);

#endif
