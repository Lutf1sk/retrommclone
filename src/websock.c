#include "websock.h"

#include <lt/net.h>
#include <lt/mem.h>

usz ws_write_frame_start(void* out, u8 op, usz len) {
	if (len < 126) {
		u8 frame[6] = {
			op, (len) | 0x80,
			0x00, 0x00, 0x00, 0x00,
		};
		memcpy(out, frame, sizeof(frame));
		return sizeof(frame);
	}
	else if (len < 65536) {
		u8 frame[8] = {
			op, 126 | 0x80,
			len >> 8, len,
			0x00, 0x00, 0x00, 0x00,
		};
		memcpy(out, frame, sizeof(frame));
		return sizeof(frame);
	}
	else {
		u8 frame[14] = {
			op, 127 | 0x80,
			len >> 56, len >> 48, len >> 40, len >> 32, len >> 24, len >> 16, len >> 8, len,
			0x00, 0x00, 0x00, 0x00,
		};
		memcpy(out, frame, sizeof(frame));
		return sizeof(frame);
	}
}

void ws_send_frame_start(lt_socket_t* sock, u8 op, usz len) {
	u8 frame[WS_FRAME_MAX];
	usz frame_len = ws_write_frame_start(frame, op, len);
	lt_socket_send(sock, frame, frame_len);
}

void ws_send_text(lt_socket_t* sock, lstr_t data) {
	ws_send_frame_start(sock, WS_FIN | WS_TEXT, data.len);
	lt_socket_send(sock, data.str, data.len);
}

