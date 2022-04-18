#include "send.h"
#include "websock.h"

#include <lt/net.h>
#include <lt/io.h>

usz send_len = 0;
char send_buf[LT_MB(1)];

void send_begin(void) {
	send_len = 0;
}

void send_end(lt_socket_t* sock) {
	if (send_len)
		lt_socket_send(sock, send_buf, send_len);
}

void send_chat(lstr_t channel, lstr_t msg) {
	send_len += ws_write_frame_start(&send_buf[send_len], WS_FIN | WS_TEXT, CLSTR("42[\"message\",{\"channel\":\"\",\"contents\":\"\"}]").len + channel.len + msg.len);
	send_len += lt_str_printf(&send_buf[send_len], "42[\"message\",{\"channel\":\"%S\",\"contents\":\"%S\"}]", channel, msg);
}

void send_click(int x, int y) {
	char tmp[32];
	usz nlen = lt_str_printiq(tmp, x) + lt_str_printiq(tmp, y);

	send_len += ws_write_frame_start(&send_buf[send_len], WS_FIN | WS_TEXT, CLSTR("42[\"mousedown\",{\"x\":.0,\"y\":.0}]").len + nlen);
	send_len += lt_str_printf(&send_buf[send_len], "42[\"mousedown\",{\"x\":%id.0,\"y\":%id.0}]", x, y);
	send_len += ws_write_frame_start(&send_buf[send_len], WS_FIN | WS_TEXT, CLSTR("42[\"mouseup\",{\"x\":.0,\"y\":.0}]").len + nlen);
	send_len += lt_str_printf(&send_buf[send_len], "42[\"mouseup\",{\"x\":%id.0,\"y\":%id.0}]", x, y);
}

void send_key_down(char* key) {
	send_len += ws_write_frame_start(&send_buf[send_len], WS_FIN | WS_TEXT, CLSTR("42[\"keydown\",\"\"]").len + strlen(key));
	send_len += lt_str_printf(&send_buf[send_len], "42[\"keydown\",\"%s\"]", key);
}

void send_key_up(char* key) {
	send_len += ws_write_frame_start(&send_buf[send_len], WS_FIN | WS_TEXT, CLSTR("42[\"keyup\",\"\"]").len + strlen(key));
	send_len += lt_str_printf(&send_buf[send_len], "42[\"keyup\",\"%s\"]", key);
}

void send_key(char* key) {
	send_key_down(key);
	send_key_up(key);
}

