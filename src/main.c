#include <lt/io.h>
#include <lt/net.h>
#include <lt/mem.h>
#include <lt/str.h>
#include <lt/json.h>
#include <lt/window.h>
#include <lt/thread.h>
#include <lt/gui.h>
#include <lt/font.h>
#include <lt/utf8.h>
#include <lt/img.h>

#include "websock.h"
#include "net_helpers.h"

#include <GL/gl.h>

#define HOST "retrommo2.herokuapp.com"
#define PORT "80"

#define USER "test@test"
#define PASS "test"

#include <time.h>

void glGenerateMipmap(GLint);

lt_arena_t* arena = NULL;
lt_socket_t* sock = NULL;
lt_window_t* win = NULL;
lt_font_t* font = NULL;

GLint icons[LT_GUI_ICON_MAX];

GLint glyph_bm;

void send_pong(lt_socket_t* sock) {
	ws_send_text(sock, CLSTR("3"));
}

void send_chat(lt_arena_t* arena, lt_socket_t* sock, lstr_t channel, lstr_t msg) {
	char* buf = lt_arena_reserve(arena, 0);
	usz len = lt_str_printf(buf, "42[\"message\",{\"channel\":\"%S\",\"contents\":\"%S\"}]", channel, msg);
	ws_send_text(sock, LSTR(buf, len));
}

void on_msg(lt_arena_t* arena, lt_socket_t* sock, lt_json_t* it) {
	if (lt_lstr_eq(it->str_val, CLSTR("play"))) {
		lt_printf("Successfully connected to '%s'\n", HOST);
		send_chat(arena, sock, CLSTR("global"), CLSTR("test"));
	}
	else if (lt_lstr_eq(it->str_val, CLSTR("update"))) {
		static b8 printed = 0;

		if (!printed) {
			lt_json_print(lt_stdout, it->next);
			printed = 1;
		}

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
	else {
		lt_werrf("Unknown or invalid message type '%S'\n", it->str_val);
	}
}

void render_init(void) {
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_SCISSOR_TEST);
}

void render_begin(lt_window_t* win) {
	int width, height;
	lt_window_get_size(win, &width, &height);

	glViewport(0, 0, width, height);
	glScissor(0, 0, width, height);

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0.0, width, height, 0.0f, -1.0f, +1.0f);
}

void render_end(lt_window_t* win) {
	lt_window_gl_swap_buffers(win);
	glFinish();
}

void render_create_tex(int w, int h, void* data, GLint* id) {
	glGenTextures(1, id);
	glBindTexture(GL_TEXTURE_2D, *id);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	glGenerateMipmap(GL_TEXTURE_2D);
}

void render_draw_rect(void* usr, usz count, lt_gui_rect_t* r, u32* clrs) {
	glBegin(GL_QUADS);
	for (usz i = 0; i < count; ++i) {
		u32 clr = clrs[i];
		u8 cr = (clr >> 16) & 0xFF;
		u8 cg = (clr >> 8) & 0xFF;
		u8 cb = clr & 0xFF;

		glColor3ub(cr, cg, cb); glVertex2f(r[i].x, r[i].y);
		glColor3ub(cr, cg, cb); glVertex2f(r[i].x + r[i].w, r[i].y);
		glColor3ub(cr, cg, cb); glVertex2f(r[i].x + r[i].w, r[i].y + r[i].h);
		glColor3ub(cr, cg, cb); glVertex2f(r[i].x, r[i].y + r[i].h);
	}
	glEnd();
}

void render_draw_text(void* usr, usz count, lt_gui_point_t* pts, lstr_t* strs, u32* clrs) {
	usz w = font->width, h = font->height;

	glBindTexture(GL_TEXTURE_2D, glyph_bm);
	glEnable(GL_TEXTURE_2D);

	float uv_w = 1.0f / font->glyph_count;

	glBegin(GL_QUADS);
	for (usz i = 0; i < count; ++i) {
		u32 clr = clrs[i];
		u8 r = (clr >> 16) & 0xFF;
		u8 g = (clr >> 8) & 0xFF;
		u8 b = clr & 0xFF;

		i32 x = pts[i].x;
		i32 y = pts[i].y;

		lstr_t text = strs[i];
		char* it = text.str, *end = text.str + text.len;
		while (it < end) {
			u32 c;
			it += lt_utf8_decode(&c, it);
			float beg = c * uv_w, end = beg + uv_w;
			glColor3ub(r, g, b); glTexCoord2f(beg, 0.0f); glVertex2f(x, y);
			glColor3ub(r, g, b); glTexCoord2f(end, 0.0f); glVertex2f(x + w, y);
			glColor3ub(r, g, b); glTexCoord2f(end, 1.0f); glVertex2f(x + w, y + h);
			glColor3ub(r, g, b); glTexCoord2f(beg, 1.0f); glVertex2f(x, y + h);

			x += w;
		}
	}
	glEnd();

	glDisable(GL_TEXTURE_2D);
}

void render_draw_icon(void* usr, usz icon, lt_gui_rect_t* r, u32 clr) {
	u8 cr = (clr >> 16) & 0xFF;
	u8 cg = (clr >> 8) & 0xFF;
	u8 cb = clr & 0xFF;

	glBindTexture(GL_TEXTURE_2D, icons[icon]);
	glEnable(GL_TEXTURE_2D);

	glBegin(GL_QUADS);
	glColor3ub(cr, cg, cb); glTexCoord2f(0.0f, 0.0f); glVertex2f(r->x, r->y);
	glColor3ub(cr, cg, cb); glTexCoord2f(1.0f, 0.0f); glVertex2f(r->x + r->w, r->y);
	glColor3ub(cr, cg, cb); glTexCoord2f(1.0f, 1.0f); glVertex2f(r->x + r->w, r->y + r->h);
	glColor3ub(cr, cg, cb); glTexCoord2f(0.0f, 1.0f); glVertex2f(r->x, r->y + r->h);
	glEnd();

	glDisable(GL_TEXTURE_2D);
}

void render_scissor(void* usr, lt_gui_rect_t* r) {
	int width, height;
	lt_window_get_size(win, &width, &height);
	glScissor(r->x, height - (r->y + r->h), r->w, r->h);
}

void load_texture(char* path, GLint* id) {
	lt_arestore_t arestore = lt_arena_save(arena);

	lstr_t tex_file;
	if (!lt_file_read_entire(arena, path, &tex_file))
		lt_ferrf("Failed to open %s\n", path);

	lt_img_t img;
	if (!lt_img_load_tga(arena, tex_file.str, tex_file.len, &img))
		lt_ferrf("Failed to load %s\n", path);

	render_create_tex(img.width, img.height, img.data, id);

	lt_arena_restore(arena, &arestore);
}

void recv_thread_proc(void* usr) {
	while (!lt_window_closed(win)) {

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
						if (it->stype == LT_JSON_STRING)
							on_msg(arena, sock, it);

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
}

int main(int argc, char** argv) {
	arena = lt_arena_alloc(LT_MB(16));

	if (!lt_window_init(arena))
		lt_ferrf("Failed to connect to window server\n");

	lt_window_description_t wdesc;
	wdesc.title = CLSTR("retrommclone");
	wdesc.x = 0;
	wdesc.y = 0;
	wdesc.w = 304*3;
	wdesc.h = 240*3;
	wdesc.output_index = 0;
	wdesc.type = LT_WIN_GL;

	win = lt_window_create(arena, &wdesc);
	if (!win)
		lt_ferrf("Window creation failed\n");

	lt_sockaddr_t saddr;
	if (!lt_sockaddr_resolve(HOST, PORT, LT_SOCKTYPE_TCP, &saddr))
		lt_ferrf("Failed to resolve '%s'\n", HOST);

	sock = lt_socket_create(arena, LT_SOCKTYPE_TCP);
	if (!sock)
		lt_ferrf("Failed to create socket\n");

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

	lstr_t font_file;
	if (!lt_file_read_entire(arena, "lat1-16.psf", &font_file))
		lt_ferr(CLSTR("Failed to open font file\n"));

	font = lt_font_load_psf(arena, font_file.str, font_file.len);
	if (!font)
		lt_ferr(CLSTR("Failed to load font\n"));

	u32 gcount = font->glyph_count;
	u8 buf[gcount * 2], *it = buf;
	for (usz i = 0; i < gcount; ++i)
		it += lt_utf8_encode(it, i);
	usz len = it - buf;

	u32* glyph_bm_buf = lt_arena_reserve(arena, 0);

	lt_font_render(font, LSTR(buf, len), glyph_bm_buf);
	render_create_tex(font->width * gcount, font->height, glyph_bm_buf, &glyph_bm);

	load_texture("expanded.tga", &icons[LT_GUI_ICON_EXPANDED]);
	load_texture("collapsed.tga", &icons[LT_GUI_ICON_COLLAPSED]);
	load_texture("check.tga", &icons[LT_GUI_ICON_CHECK]);

	lt_gui_ctx_t gcx, *cx = &gcx;
	gcx.cont_max = 128;
	gcx.draw_rect = render_draw_rect;
	gcx.draw_text = render_draw_text;
	gcx.draw_icon = render_draw_icon;
	gcx.scissor = render_scissor;
	gcx.glyph_height = font->height;
	gcx.glyph_width = font->width;

	lt_gui_ctx_init(arena, cx);

	lt_thread_t* recv_thread = lt_thread_create(arena, recv_thread_proc, NULL);

	while (!lt_window_closed(win)) {
		render_init();

		lt_window_event_t ev[16];
		lt_window_poll_events(win, ev, 16);

		render_begin(win);

		int width, height;
		lt_window_get_size(win, &width, &height);

		lt_gui_begin(cx, width, height);

		lt_gui_panel_begin(cx, 0, 0, 0);
		lt_gui_label(cx, CLSTR("RetroMMClone"), 0);
		lt_gui_panel_end(cx);

		lt_gui_end(cx);

		render_end(win);
	}

	lt_thread_join(recv_thread);

	lt_socket_destroy(sock);

	lt_window_destroy(win);

	lt_window_terminate();
}

