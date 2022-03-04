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
#include <lt/ctype.h>

#include "websock.h"
#include "net_helpers.h"

#include <GL/gl.h>

#define HOST "retrommo2.herokuapp.com"
#define PORT "80"

#define USER "test2@test"
#define PASS "test"

#include <time.h>

#define USERNAME_MAXLEN 18
#define USERSLUG_MAXLEN 21

void glGenerateMipmap(GLint);

lt_arena_t* arena = NULL;
lt_socket_t* sock = NULL;
lt_window_t* win = NULL;
lt_font_t* font = NULL;

GLint icons[LT_GUI_ICON_MAX];
GLint glyph_bm;

int local_playerid = 0;

typedef
struct player {
	lstr_t slug;
	lstr_t username;
	u8 direction;
	int x, y;
} player_t;

#define MAX_PLAYERS 64

player_t players[MAX_PLAYERS];
int player_count = 0;

char player_usernames[MAX_PLAYERS][USERNAME_MAXLEN];
char player_slugs[MAX_PLAYERS][USERSLUG_MAXLEN];

typedef
struct character {
	lstr_t class_name;
	lstr_t description;
	u8 page;
	u8 level;
	b8 present;
} character_t;

#define MAX_CHARACTERS 21

character_t characters[MAX_CHARACTERS];
int character_count = 0;
char character_descriptions[MAX_CHARACTERS][32];
u8 sel_charid = 0;

#define MAX_INV_ITEMS 8
lstr_t inv_items[MAX_INV_ITEMS];
char inv_item_names[MAX_INV_ITEMS][32];
int inv_item_count = 0;

lstr_t gold;
char gold_buf[32];

#define MAX_MSGS 256
lstr_t chat_msgs[MAX_MSGS];
int chat_msg_count = 0;

void send_pong(lt_socket_t* sock) {
	ws_send_text(sock, CLSTR("3"));
}

lt_spinlock_t state_lock = {0};

void send_chat(lt_arena_t* arena, lt_socket_t* sock, lstr_t channel, lstr_t msg) {
	char* buf = lt_arena_reserve(arena, 0);
	usz len = lt_str_printf(buf, "42[\"message\",{\"channel\":\"%S\",\"contents\":\"%S\"}]", channel, msg);
	ws_send_text(sock, LSTR(buf, len));
}

b8 lstr_startswith(lstr_t str, lstr_t substr) {
	if (str.len < substr.len)
		return 0;
	return memcmp(str.str, substr.str, substr.len) == 0;
}

b8 lstr_endswith(lstr_t str, lstr_t substr) {
	if (str.len < substr.len)
		return 0;

	char* end = str.str + str.len;
	return memcmp(end - substr.len, substr.str, substr.len) == 0;
}

player_t* find_player_from_slug(lstr_t slug) {
	for (usz i = 0; i < player_count; ++i)
		if (lt_lstr_eq(slug, players[i].slug))
			return &players[i];

	LT_ASSERT_NOT_REACHED();
	return NULL;
}

#define CMD_SWITCH_CHAR 1
#define CMD_GET_CHARS	2
u8 cmd = CMD_GET_CHARS;
u8 cmd_charid = 0;
int cmd_start_page = -1;
b8 cmd_pages_wrapped = 0;

#define RETRO_CHARSEL	0
#define RETRO_WORLD		1
#define RETRO_INVENTORY	2
#define RETRO_SPELLBOOK	3

u8 retro_state;

void switch_char(u8 charid) {
	cmd = CMD_SWITCH_CHAR;
	cmd_charid = charid;

	lt_printf("Selecting character %ud on page %ud\n", charid % 7, characters[charid].page);

	LT_ASSERT(charid < character_count);
}

void send_click(lt_arena_t* arena, int x, int y) {
	char* msg_buf = lt_arena_reserve(arena, 0);
	usz msg_len = lt_str_printf(msg_buf, "42[\"mousedown\",{\"x\":%id.0,\"y\":%id.0}]", x, y);
	ws_send_text(sock, LSTR(msg_buf, msg_len));

	msg_len = lt_str_printf(msg_buf, "42[\"mouseup\",{\"x\":%id.0,\"y\":%id.0}]", x, y);
	ws_send_text(sock, LSTR(msg_buf, msg_len));
}

void send_key(lt_arena_t* arena, char key) {
	char* msg_buf = lt_arena_reserve(arena, 0);
	usz msg_len = lt_str_printf(msg_buf, "42[\"keydown\",\"%c\"]", key);
	ws_send_text(sock, LSTR(msg_buf, msg_len));

	msg_len = lt_str_printf(msg_buf, "42[\"keyup\",\"%c\"]", key);
	ws_send_text(sock, LSTR(msg_buf, msg_len));
}

void on_msg(lt_arena_t* arena, lt_socket_t* sock, lt_json_t* it) {
	if (lt_lstr_eq(it->str_val, CLSTR("play"))) {
		lt_printf("Successfully connected to '%s'\n", HOST);
	}
	else if (lt_lstr_eq(it->str_val, CLSTR("update"))) {
		lt_spinlock_lock(&state_lock);

		send_key(arena, 'c');

		lstr_t local_player_slug = lt_json_find_child(it->next, CLSTR("playerSlug"))->str_val;

		lt_json_t* pieces = lt_json_find_child(it->next, CLSTR("pieces"));
		lt_json_t* piece_it = pieces->child;
		player_count = 0;

		lt_json_t* logout = lt_json_find_child(pieces, CLSTR("Switch|picture/world-logout"));

		lt_json_t* botbar_inv = lt_json_find_child(pieces, CLSTR("Picture|bottom-bar-icon/inventory"));
		lt_json_t* botbar_spellb = lt_json_find_child(pieces, CLSTR("Picture|bottom-bar-icon/spellbook"));

		if (lt_json_find_child(pieces, CLSTR("Label|character-select-title")))
			retro_state = RETRO_CHARSEL;
		else if (logout) {
			lstr_t inv_img_slug = lt_json_find_child(botbar_inv, CLSTR("imageSourceSlug"))->str_val;
			lstr_t spellb_img_slug = lt_json_find_child(botbar_spellb, CLSTR("imageSourceSlug"))->str_val;

			if (lt_lstr_eq(inv_img_slug, CLSTR("bottom-bar-icons/inventory-selected")))
				retro_state = RETRO_INVENTORY;
			else if (lt_lstr_eq(spellb_img_slug, CLSTR("bottom-bar-icons/spellbook-selected")))
				retro_state = RETRO_SPELLBOOK;
			else
				retro_state = RETRO_WORLD;
		}
		else
			LT_ASSERT_NOT_REACHED();

// 		lt_json_print(lt_stdout, it->next);

		static int last_pageid = -1;
		static b8 awaiting_pageswitch = 0;
		u32 charsel_pageid = 0;

		if (retro_state == RETRO_CHARSEL) {
			lt_json_t* rarrow = lt_json_find_child(pieces, CLSTR("Switch|picture/character-customize-page-left"));
			lt_json_t* page = lt_json_find_child(pieces, CLSTR("Label|character-select-page"));

			if (page) {
				lstr_t pagelbl = lt_json_find_child(page, CLSTR("text"))->str_val;
				usz pfx_len = CLSTR("Page ").len;
				charsel_pageid = lt_lstr_uint(LSTR(pagelbl.str + pfx_len, pagelbl.len - pfx_len)) - 1;
			}

			if (cmd == CMD_GET_CHARS) {
				if (cmd_start_page == -1)
					cmd_start_page = charsel_pageid;
				else if (charsel_pageid != cmd_start_page || !page || !rarrow)
					cmd_pages_wrapped = 1;

				if (cmd_start_page == charsel_pageid && cmd_pages_wrapped)
					cmd = 0;
			}

			if (charsel_pageid != last_pageid)
				awaiting_pageswitch = 0;

			if (cmd == CMD_SWITCH_CHAR && characters[cmd_charid].page == charsel_pageid) {
				char* key_buf = lt_arena_reserve(arena, 0);
				usz key_len = lt_str_printf(key_buf, "Switch|picture/character-select-character-%ud-play", cmd_charid % 7);
				lt_json_t* play = lt_json_find_child(pieces, LSTR(key_buf, key_len));

				lt_printf("Sending 'character select' click on page %ud\n", charsel_pageid);

				int x = lt_json_int_val(lt_json_find_child(play, CLSTR("x")));
				int y = lt_json_int_val(lt_json_find_child(play, CLSTR("y")));

				send_click(arena, x + 1, y + 1);

 				sel_charid = cmd_charid;
 				cmd = 0;
 				inv_item_count = 0;
 				gold = NLSTR();
			}
			else if (cmd == CMD_GET_CHARS || (cmd == CMD_SWITCH_CHAR && charsel_pageid != characters[cmd_charid].page)) {
				if (!awaiting_pageswitch) {
					lt_printf("Sending 'next page' click on page %ud\n", charsel_pageid);
					send_click(arena, 223, 203);
					awaiting_pageswitch = 1;
				}
			}
		}
		else if (retro_state == RETRO_WORLD || retro_state == RETRO_INVENTORY || retro_state == RETRO_SPELLBOOK) {
			if (cmd == CMD_SWITCH_CHAR || cmd == CMD_GET_CHARS) {
				int x = lt_json_int_val(lt_json_find_child(logout, CLSTR("x")));
				int y = lt_json_int_val(lt_json_find_child(logout, CLSTR("y")));

				send_click(arena, x + 1, y + 1);
			}
		}

		while (piece_it) {
			if (lstr_startswith(piece_it->key, CLSTR("Player|"))) {
				usz pfx_len = CLSTR("Player|").len;

				lstr_t slug = LSTR(piece_it->key.str + pfx_len, piece_it->key.len - pfx_len);
				lstr_t username = lt_json_find_child(piece_it, CLSTR("username"))->str_val;

				memcpy(player_slugs[player_count], slug.str, slug.len);
				memcpy(player_usernames[player_count], username.str, username.len);

				players[player_count].username = LSTR(player_usernames[player_count], username.len);
				players[player_count].slug = LSTR(player_slugs[player_count], slug.len);

				if (lt_lstr_eq(slug, local_player_slug))
					local_playerid = player_count;

				player_count++;
			}
			else if (cmd == CMD_GET_CHARS && lstr_startswith(piece_it->key, CLSTR("Label|character-select-character-"))) {
				usz pfx_len = CLSTR("Label|character-select-character-").len;
				usz char_id = lt_lstr_uint(LSTR(piece_it->key.str + pfx_len, piece_it->key.len - pfx_len));

				char_id += charsel_pageid * 7;

				if (char_id + 1 > character_count)
					character_count = char_id + 1;

				lstr_t name = lt_json_find_child(piece_it, CLSTR("text"))->str_val;

				char* lv_begin = name.str + CLSTR("Lv").len, *lv_it = lv_begin;
				while (lt_is_digit(*lv_it))
					++lv_it;
				characters[char_id].level = lt_lstr_uint(LSTR(lv_begin, lv_it - lv_begin));
				if (lstr_endswith(name, CLSTR("WR")))
					characters[char_id].class_name = CLSTR("Warrior");
				else if (lstr_endswith(name, CLSTR("WZ")))
					characters[char_id].class_name = CLSTR("Wizard");
				else if (lstr_endswith(name, CLSTR("CL")))
					characters[char_id].class_name = CLSTR("Cleric");
				characters[char_id].description.str = character_descriptions[char_id];
				characters[char_id].description.len = lt_str_printf(character_descriptions[char_id], "%ud %S", char_id + 1, characters[char_id].class_name);
				characters[char_id].page = charsel_pageid;
				characters[char_id].present = 1;
			}
			else if (retro_state == RETRO_INVENTORY && lstr_startswith(piece_it->key, CLSTR("Label|world-inventory-bag-"))) {
				usz pfx_len = CLSTR("Label|world-inventory-bag-").len;
				usz slot_id = lt_lstr_uint(LSTR(piece_it->key.str + pfx_len, piece_it->key.len - pfx_len));

				if (slot_id + 1 > inv_item_count)
					inv_item_count = slot_id + 1;

				lstr_t name = lt_json_find_child(piece_it, CLSTR("text"))->str_val;
				inv_items[slot_id].str = inv_item_names[slot_id];
				inv_items[slot_id].len = name.len;
				memcpy(inv_item_names[slot_id], name.str, name.len);
			}
			piece_it = piece_it->next;
		}

		lt_json_t* chats = lt_json_find_child(it->next, CLSTR("chats"));
		lt_json_t* chat_it = chats->child;
		while (chat_it) {
			lstr_t type = lt_json_find_child(chat_it, CLSTR("type"))->str_val;
			lstr_t player_slug = lt_json_find_child(chat_it, CLSTR("playerSlug"))->str_val;

			char* chat_buf = lt_arena_reserve(arena, 0);
			usz chat_len = 0;

			if (lt_lstr_eq(type, CLSTR("message"))) {
				lstr_t username = find_player_from_slug(player_slug)->username;
				lstr_t msg = lt_json_find_child(chat_it, CLSTR("contents"))->str_val;
// 				lstr_t channel = lt_json_find_child(chat_it, CLSTR("channel"))->str_val;
				chat_len = lt_str_printf(chat_buf, "[%S] %S", username, msg);
			}
			else if (lt_lstr_eq(type, CLSTR("login"))) {
				lstr_t username = find_player_from_slug(player_slug)->username;
				chat_len = lt_str_printf(chat_buf, "%S has logged in", username);
			}
			else if (lt_lstr_eq(type, CLSTR("logout"))) {
// 				chat_len = lt_str_printf(chat_buf, "%S has logged out", username);
				continue;
			}

			chat_msgs[chat_msg_count].str = malloc(chat_len);
			chat_msgs[chat_msg_count].len = chat_len;
			memcpy(chat_msgs[chat_msg_count].str, chat_buf, chat_len);
			chat_msg_count++;

			chat_it = chat_it->next;
		}

		if (retro_state == RETRO_INVENTORY) {
			lt_json_t* gold_json = lt_json_find_child(pieces, CLSTR("Label|world-inventory-gold"));
			lstr_t gold_str = lt_json_find_child(gold_json, CLSTR("text"))->str_val;
			gold.str = gold_buf;
			gold.len = gold_str.len;
			memcpy(gold_buf, gold_str.str, gold_str.len);
		}

		last_pageid = charsel_pageid;

		lt_spinlock_release(&state_lock);
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
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

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

b8 quit = 0;

void recv_thread_proc(void* usr) {
	while (!lt_window_closed(win) && !quit) {
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
				quit = 1;
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

	// POST /authenticate

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

	// GET /
	out_len = lt_str_printf(out_buf,
		"GET / HTTP/1.1\r\n"
		"Host: %s\r\n"
		"Connection: keep-alive\r\n"
		"\r\n",
		HOST
	);
	lt_socket_send(sock, out_buf, out_len);

	char* html_str = lt_arena_reserve(arena, 0);
	usz html_len = 0;
	handle_http_response(arena, sock, html_str, &html_len);
	lt_arena_reserve(arena, html_len);

	// Find beginning of data-definitions attribute
	lstr_t ddef_signature = CLSTR("data-definitions=\"");
	char* defs_start = NULL;
	char* defs_it = html_str, *html_end = html_str + html_len - ddef_signature.len;
	while (defs_it < html_end) {
		if (*defs_it == 'd' && lt_lstr_eq(LSTR(defs_it, ddef_signature.len), ddef_signature)) {
			defs_start = defs_it + ddef_signature.len;
			defs_it += ddef_signature.len;
			break;
		}
		++defs_it;
	}

	if (!defs_start)
		lt_ferrf("Failed to find data definitions\n");

	// Find end of data-definitions attribute
	usz defs_len = 0;
	while (defs_it < html_end) {
		if (*defs_it == '"') {
			defs_len = defs_it - defs_start;
			break;
		}
		++defs_it;
	}

	if (!defs_len)
		lt_ferrf("Failed to find end of data definitions\n");

	// Unescape data-definitions json
	char* ddefs = lt_arena_reserve(arena, 0);
	char* ddefs_it = ddefs;
	for (usz i = 0; i < defs_len;) {
		char c = defs_start[i++];
		if (c == '&') {
			char* escape_start = &defs_start[i];
			usz escape_len = 0;
			while (i < defs_len && defs_start[i++] != ';')
				++escape_len;
			lstr_t escape = LSTR(escape_start, escape_len);
			if (lt_lstr_eq(CLSTR("quot"), escape))
				*ddefs_it++ = '"';
			else if (lt_lstr_eq(CLSTR("#x2F"), escape))
				*ddefs_it++ = '/';
			else
				lt_werrf("Unknown escape sequence '%S'\n", escape);
		}
		else
			*ddefs_it++ = c;
	}
	usz ddefs_len = ddefs_it - ddefs;
	lt_arena_reserve(arena, ddefs_len);

// 	lt_printf("%S\n", LSTR(ddefs, ddefs_len));

	lt_json_t* ddefs_js = lt_json_parse(arena, ddefs, ddefs_len);

	lt_json_t* ddef_it = ddefs_js->child;
	while (ddef_it) {
		lt_printf("%S\n", ddef_it->key);
		ddef_it = ddef_it->next;
	}

// 	lt_json_print(lt_stdout, lt_json_find_child(ddefs_js, CLSTR("Tilemap|inn")));

	// Upgrade to websocket

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

	#define SIDEBAR_W 225

	b8 playerlist_state = 0;
	u32 charlist_state = 0;

	render_init();

	while (!lt_window_closed(win) && !quit) {
		lt_window_event_t ev[16];
		lt_window_poll_events(win, ev, 16);

		lt_window_mouse_pos(win, &cx->mouse_x, &cx->mouse_y);
		cx->mouse_state = lt_window_key_pressed(win, LT_KEY_MB1);

		render_begin(win);

		int width, height;
		lt_window_get_size(win, &width, &height);

		lt_spinlock_lock(&state_lock);

		lt_gui_begin(cx, width, height);

		lt_gui_panel_begin(cx, 0, 0, 0);

		lt_gui_row(cx, 5);
		if (lt_gui_dropdown_begin(cx, CLSTR("Character"), 97, character_count * 18, &charlist_state, 0)) {
			for (usz i = 0; i < character_count; ++i) {
				if (lt_gui_button(cx, characters[i].description, LT_GUI_ALIGN_RIGHT | LT_GUI_BORDER_OUTSET | LT_GUI_GROW_X))
					switch_char(i);
			}
			lt_gui_dropdown_end(cx);
		}

		lt_gui_hspace(cx, 4, 0);
		lt_gui_label(cx, players[local_playerid].username, 0);

		if (lt_gui_button(cx, CLSTR("Log out"), LT_GUI_ALIGN_RIGHT | LT_GUI_BORDER_OUTSET))
			quit = 1;
		if (lt_gui_button(cx, CLSTR("Settings"), LT_GUI_ALIGN_RIGHT | LT_GUI_BORDER_OUTSET))
			;

		lt_gui_row(cx, 2);
		lt_gui_panel_begin(cx, -SIDEBAR_W, 0, LT_GUI_BORDER_INSET);
		lt_gui_label(cx, CLSTR("Inventory:"), 0);
		for (usz i = 0; i < inv_item_count; ++i) {
			lt_gui_row(cx, 2);
			lt_gui_label(cx, CLSTR(" - "), 0);
			lt_gui_label(cx, inv_items[i], 0);
		}
		lt_gui_label(cx, gold, 0);
		lt_gui_panel_end(cx);

		lt_gui_panel_begin(cx, 0, 0, LT_GUI_BORDER_INSET);
		if (lt_gui_expandable(cx, CLSTR("Players"), &playerlist_state, LT_GUI_BORDER_OUTSET)) {
			lt_gui_panel_begin(cx, 0, (font->height) * player_count + 6 + (2 * player_count - 1), LT_GUI_BORDER_OUTSET);
			for (usz i = 0; i < player_count; ++i)
				lt_gui_label(cx, players[i].username, 0);
			lt_gui_panel_end(cx);
		}

		lt_gui_panel_begin(cx, 0, 0, LT_GUI_BORDER_OUTSET);
		for (usz i = 0; i < chat_msg_count; ++i)
			lt_gui_label(cx, chat_msgs[i], 0);
		lt_gui_panel_end(cx);

		lt_gui_panel_end(cx);

		lt_gui_panel_end(cx);

		lt_gui_end(cx);

		lt_spinlock_release(&state_lock);

		render_end(win);
	}

	lt_thread_join(recv_thread);

	lt_socket_destroy(sock);

	lt_window_destroy(win);

	lt_window_terminate();
}

