#include "resource.h"

#include "config.h"
#include "render.h"
#include "net_helpers.h"

#include "stb_image.h"

#include <lt/io.h>

b8 res_load(lt_arena_t* arena, lstr_t path, lstr_t* out) {
	char path_buf[512];
	lt_str_printf(path_buf, "./cache/%S%c", path, 0);
	char* it = path_buf + CLSTR("./cache/").len;
	while (*it) {
		if (*it == '/')
			*it = '_';
		++it;
	}
	lstr_t data;

	if (!lt_file_read_entire(arena, path_buf, &data)) {
		lt_printf("Downloading '%S'...\n", path);

		lt_sockaddr_t saddr;
		if (!lt_sockaddr_resolve(HOST, PORT, LT_SOCKTYPE_TCP, &saddr))
			lt_ferrf("Failed to resolve '%s'\n", HOST);

		lt_socket_t* sock = lt_socket_create(arena, LT_SOCKTYPE_TCP);
		if (!sock)
			lt_ferrf("Failed to create socket\n");

		if (!lt_socket_connect(sock, &saddr))
			lt_ferrf("Failed to connect to '%s:%s'\n", HOST, PORT);

		char out_buf[512];
		usz out_len = lt_str_printf(out_buf,
			"GET %S HTTP/1.1\r\n"
			"Connection: close\r\n"
			"Host: %s\r\n"
			"\r\n",
			path, HOST
		);
		lt_socket_send(sock, out_buf, out_len);
		void* buf = lt_arena_reserve(arena, 0);
		usz len = 0;
		handle_http_response(arena, sock, buf, &len);
		lt_arena_reserve(arena, len);

		data = LSTR(buf, len);

		lt_socket_destroy(sock);

		lt_file_t* file = lt_file_open(arena, path_buf, LT_FILE_W, 0);
		if (file) {
			lt_file_write(file, data.str, data.len);
			lt_file_close(file);
		}
		else
			lt_printf("Failed to cache image\n");
	}
	else
		lt_printf("Loading cached file '%S'\n", path);

	*out = data;
	return 1;
}

b8 res_load_texture(lt_arena_t* arena, lstr_t slug, int* out) {
	lstr_t data;

	char path_buf[512];
	usz path_len = lt_str_printf(path_buf, "/images/game/%S.png", slug);
	if (!res_load(arena, LSTR(path_buf, path_len), &data))
		return 0;

	int w, h, channels;
	u8* pixels = stbi_load_from_memory(data.str, data.len, &w, &h, &channels, 4);

	if (!pixels)
		return 0;

	render_create_tex(w, h, pixels, out, 0);
	return 1;
}

