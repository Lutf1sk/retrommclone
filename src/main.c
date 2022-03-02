#include <lt/io.h>
#include <lt/net.h>
#include <lt/mem.h>
#include <lt/str.h>
#include <lt/json.h>

#define HOST "retrommo2.herokuapp.com"
#define PORT "80"

#define USER "test@test"
#define PASS "test"

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

#include <time.h>

#define BIT(n) (1 << n)

isz recv_fixed(lt_socket_t* sock, void* data, isz bytes) {
	u8* it = data;
	isz rem = bytes;
	while (rem > 0) {
		isz res = lt_socket_recv(sock, it, rem);
		if (res <= 0)
			return res;

		it += res;
		rem -= res;
	}
	return bytes;
}

void ws_send_frame_start(lt_socket_t* sock, u8 op, usz len) {
	if (len < 126) {
		u8 frame[6] = {
			op, (len) | 0x80,
			0x00, 0x00, 0x00, 0x00,
		};
		lt_socket_send(sock, frame, sizeof(frame));
	}
	else if (len < 65536) {
		u8 frame[8] = {
			op, 126 | 0x80,
			len >> 8, len,
			0x00, 0x00, 0x00, 0x00,
		};
		lt_socket_send(sock, frame, sizeof(frame));
	}
	else {
		u8 frame[14] = {
			op, 127 | 0x80,
			len >> 56, len >> 48, len >> 40, len >> 32, len >> 24, len >> 16, len >> 8, len,
			0x00, 0x00, 0x00, 0x00,
		};
		lt_socket_send(sock, frame, sizeof(frame));
	}
}

void ws_send_text(lt_socket_t* sock, lstr_t data) {
	ws_send_frame_start(sock, WS_FIN | WS_TEXT, data.len);
	lt_socket_send(sock, data.str, data.len);
}

void send_pong(lt_socket_t* sock) {
	ws_send_text(sock, CLSTR("3"));
}

void handle_http_response(lt_arena_t* arena, lt_socket_t* sock, void* content, usz* content_len) {
	char c = 0;
	while (c != '\n')
		lt_socket_recv(sock, &c, 1);

	usz len = 0;

	// TODO: This is horrible, fix this 

	while (1) {
		char* line = lt_arena_reserve(arena, 0);
		usz line_len = 0;

		do {
			lt_socket_recv(sock, &c, 1);
			line[line_len++] = c;
		} while (c != '\n');

		if (line_len <= 2)
			break;

		char* it = line, *key = line;
		while (*it++ != ':')
			;
		usz key_len = it - key - 1;

		++it; // Skip ' '

		char* val = it;
		while (*it++ != '\r')
			;
		usz val_len = it - val - 1;

		if (lt_lstr_eq(LSTR(key, key_len), CLSTR("Content-Length")))
			len = lt_lstr_uint(LSTR(val, val_len));
	}

	recv_fixed(sock, content, len);
	if (content_len)
		*content_len = len;
}

void send_chat(lt_arena_t* arena, lt_socket_t* sock, lstr_t channel, lstr_t msg) {
	char* buf = lt_arena_reserve(arena, 0);
	usz len = lt_str_printf(buf, "42[\"message\",{\"channel\":\"%S\",\"contents\":\"%S\"}]", channel, msg);
	ws_send_text(sock, LSTR(buf, len));
}

int main(int argc, char** argv) {
	lt_arena_t* arena = lt_arena_alloc(LT_MB(16));

	lt_sockaddr_t saddr;
	if (!lt_sockaddr_resolve(HOST, PORT, LT_SOCKTYPE_TCP, &saddr))
		lt_ferrf("Failed to resolve '%s'\n", HOST);

	lt_socket_t* sock = lt_socket_create(arena, LT_SOCKTYPE_TCP);

	if (!lt_socket_connect(sock, &saddr))
		lt_ferrf("Failed to connect to '%s:%s'\n", HOST, PORT);

	lstr_t login_json = CLSTR("{\"email\":\""USER"\",\"password\":\""PASS"\",\"hcaptchaToken\":\"\"}");

	char* out_buf = lt_arena_reserve(arena, 0);
	usz out_len = lt_str_printf(out_buf,
		"POST /authenticate HTTP/1.1\r\n"
		"Host: %s\r\n"
		"Content-Length: %ud\r\n"
		"Content-Type: application/json\r\n"
		"Accept: application/json\r\n"
		"Connection: keep-alive\r\n"
		"\r\n"
		"%S",
		HOST, login_json.len, login_json
	);
	lt_socket_send(sock, out_buf, out_len);

	char token_buf[32] = {0};
	lstr_t token = LSTR(token_buf, 0);
	handle_http_response(arena, sock, token_buf, &token.len);

	lt_printf("Got login token: %S\n", token);

	out_len = lt_str_printf(out_buf,
		"GET /socket.io/?EIO=4&transport=websocket&t=%ud HTTP/1.1\r\n"
		"Connection: Upgrade\r\n"
		"Host: %s\r\n"
		"Sec-WebSocket-Key: %s\r\n"
		"Sec-WebSocket-Version: 13\r\n"
		"Upgrade: websocket\r\n"
		"User-Agent: WebSocket++/0.8.2\r\n"
		"\r\n",
		time(NULL), HOST, WS_KEY
	);
	lt_socket_send(sock, out_buf, out_len);
	handle_http_response(arena, sock, NULL, NULL);

	out_len = lt_str_printf(out_buf, "40{\"token\":\"%S\"}", token);
	ws_send_text(sock, LSTR(out_buf, out_len));

	while (1) {
		lt_arestore_t arestore = lt_arena_save(arena);

		u8 frame[8];
		isz bytes = recv_fixed(sock, frame, 2);

		if (bytes == 0) {
			lt_printf("Socket closed\n");
			goto closed;
		}
		else if (bytes < 0)
			lt_ferrf("Failed to read from socket: %s\n", lt_os_err_str());

		u8 op = frame[0] & WS_OP_MASK;
		usz payload_len = frame[1] & 0x7F;

		if (payload_len == 126) {
			recv_fixed(sock, frame, 2);
			payload_len = frame[1] | (frame[0] << 8);
		}
		else if (payload_len == 127) {
			recv_fixed(sock, frame, 8);
			payload_len = (u64)frame[7] | ((u64)frame[6] << 8) | ((u64)frame[5] << 16) | ((u64)frame[4] << 24) |
					 ((u64)frame[3] << 32) | ((u64)frame[2] << 40) | ((u64)frame[1] << 48) | ((u64)frame[0] << 56);
		}

		LT_ASSERT(payload_len < LT_MB(1));

		char* payload = lt_arena_reserve(arena, payload_len);

		recv_fixed(sock, payload, payload_len);

		if (payload_len < 1)
			goto done;

		char type = payload[0];
		if (type == '2') {
			send_pong(sock);
			goto done;
		}

		lt_json_t* json = lt_json_parse(arena, payload + 2, payload_len - 2);
		if (!json)
			lt_werrf("Malformed message\n");
		else {
			switch (op) {
			case WS_TEXT: {
				if (json->stype == LT_JSON_ARRAY || json->stype == LT_JSON_OBJECT) {
					lt_json_t* it = json->child;
					while (it) {
						if (it->stype == LT_JSON_STRING) {
							if (lt_lstr_eq(it->str_val, CLSTR("play"))) {
								lt_printf("Successfully connected to '%s'\n", HOST);
								send_chat(arena, sock, CLSTR("global"), CLSTR("test"));
							}
							if (lt_lstr_eq(it->str_val, CLSTR("update"))) {
								lt_json_t* pieces = lt_json_find_child(it->next, CLSTR("pieces"));
								lt_json_t* chats = lt_json_find_child(it->next, CLSTR("chats"));

								lt_json_t* chat_it = chats->child;
								while (chat_it) {
									lstr_t type = lt_json_find_child(chat_it, CLSTR("type"))->str_val;
									if (lt_lstr_eq(type, CLSTR("message"))) {
										lstr_t msg = lt_json_find_child(chat_it, CLSTR("contents"))->str_val;
										lstr_t channel = lt_json_find_child(chat_it, CLSTR("channel"))->str_val;
										lstr_t player_slug = lt_json_find_child(chat_it, CLSTR("playerSlug"))->str_val;

										char* key_buf = lt_arena_reserve(arena, 0);
										usz key_len = lt_str_printf(key_buf, "Player|%S", player_slug);
										lt_json_t* player = lt_json_find_child(pieces, LSTR(key_buf, key_len));

										lstr_t name = lt_json_find_child(player, CLSTR("username"))->str_val;

										lt_printf("(%S)[%S]: %S\n", channel, name, msg);
									}

									chat_it = chat_it->next;
								}
							}
						}

						it = it->next;
					}
				}
			}	break;

			case WS_CLOSE:
				ws_send_frame_start(sock, WS_FIN | WS_CLOSE, 0);
				goto closed;

			case WS_PING:
				ws_send_frame_start(sock, WS_FIN | WS_PONG, 0);
				break;
			}
		}

done:
		lt_arena_restore(arena, &arestore);
	}
closed:

	lt_socket_destroy(sock);
}

