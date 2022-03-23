#ifndef NET_HELPERS_H
#define NET_HELPERS_H 1

#include <lt/net.h>
#include <lt/mem.h>
#include <lt/str.h>

static
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

static
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

#endif
