#include <lt/io.h>
#include <lt/net.h>
#include <lt/mem.h>
#include <lt/str.h>
#include <lt/json.h>
#include <lt/window.h>
#include <lt/thread.h>
#include <lt/gui.h>
#include <lt/font.h>
#include <lt/img.h>
#include <lt/utf8.h>
#include <lt/ctype.h>

#include "websock.h"
#include "net_helpers.h"
#include "render.h"
#include "config.h"
#include "resource.h"

#include <time.h>
#include <GL/gl.h>

#define USERNAME_MAXLEN 18
#define USERSLUG_MAXLEN 21

typedef struct tilemap tilemap_t;

lt_arena_t* arena = NULL;
lt_socket_t* sock = NULL;
lt_window_t* win = NULL;
lt_font_t* font = NULL;

int icons[LT_GUI_ICON_MAX];
int glyph_bm;

int local_playerid = 0;

#define DIR_DOWN	0
#define DIR_LEFT	1
#define DIR_RIGHT	2
#define DIR_UP		3

typedef
struct mask {
	lstr_t slug;
	int texture_m;
	int texture_f;
} mask_t;

#define MAX_MASKS 256

mask_t masks[MAX_MASKS];
usz mask_count = 0;

typedef
struct outfit {
	lstr_t slug;
	int texture_m;
	int texture_f;
} outfit_t;

#define MAX_OUTFITS 256

outfit_t outfits[MAX_OUTFITS];
usz outfit_count = 0;

#define FIGURE_M 0
#define FIGURE_F 0

typedef
struct player {
	lstr_t slug;
	lstr_t username;
	u8 direction;
	int x, y;
	tilemap_t* tilemap;

	u8 figure;
	mask_t* mask;
	outfit_t* outfit;

	u64 walk_start;
} player_t;

#define MAX_PLAYERS 128

player_t players[MAX_PLAYERS];
int player_count = 0;

char player_usernames[MAX_PLAYERS][USERNAME_MAXLEN];
char player_slugs[MAX_PLAYERS][USERSLUG_MAXLEN];

#define MOVESPEED 250.0f

typedef
struct chest {
	lstr_t slug;
	i32 opened_at;
	int texture;
} chest_t;

#define MAX_CHESTS 128

chest_t chests[MAX_CHESTS];
usz chest_count = 0;

typedef
struct bank {
	lstr_t slug;
	i32 opened_at;
	i32 closed_at;
	int texture;
} bank_t;

#define MAX_BANKS 128

bank_t banks[MAX_BANKS];
usz bank_count = 0;

typedef
struct npc {
	lstr_t slug;
	lstr_t name;
	int texture;
	int indicator_texture;
	u8 direction;
} npc_t;

#define MAX_NPCS 128

npc_t npcs[MAX_NPCS];
usz npc_count = 0;

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


typedef
struct tileset {
	lstr_t name;
	b8* collisions;
	i8* chests, *banks, *npcs;
	usz tile_count;
	usz width;
	usz height;

	int texture;
} tileset_t;

#define MAX_TILESETS 32

tileset_t tilesets[MAX_TILESETS];
char tileset_names[MAX_TILESETS][32];
usz tileset_count = 0;

typedef
struct tilemap {
	lstr_t name;
	tileset_t** tilesets;
	u16* tileset_start_indices;
	usz tileset_count;

	usz w, h;
	u16* tiles, *a_tile_indices, *b_tile_indices;
	i16* chest_indices, *bank_indices, *npc_indices;
} tilemap_t;

#define MAX_TILEMAPS 16

tilemap_t tilemaps[MAX_TILEMAPS];
char tilemap_names[MAX_TILEMAPS][32];
usz tilemap_count = 0;

void send_pong(lt_socket_t* sock) {
	ws_send_text(sock, CLSTR("3"));
}

lt_mutex_t* state_lock;

void send_chat(lt_arena_t* arena, lt_socket_t* sock, lstr_t channel, lstr_t msg) {
	char* buf = lt_arena_reserve(arena, 0);
	usz len = lt_str_printf(buf, "42[\"message\",{\"channel\":\"%S\",\"contents\":\"%S\"}]", channel, msg);
	ws_send_text(sock, LSTR(buf, len));
}

player_t* find_player_from_slug(lstr_t slug) {
	for (usz i = 0; i < player_count; ++i)
		if (lt_lstr_eq(slug, players[i].slug))
			return &players[i];

	LT_ASSERT_NOT_REACHED();
	return NULL;
}

tilemap_t* find_tilemap(lstr_t slug) {
	for (usz i = 0; i < tilemap_count; ++i)
		if (lt_lstr_eq(slug, tilemaps[i].name))
			return &tilemaps[i];
	return NULL;
}

tileset_t* find_tileset(lstr_t slug) {
	for (usz i = 0; i < tileset_count; ++i)
		if (lt_lstr_eq(slug, tilesets[i].name))
			return &tilesets[i];
	return NULL;
}

mask_t* find_mask(lstr_t slug) {
	for (usz i = 0; i < mask_count; ++i)
		if (lt_lstr_eq(slug, masks[i].slug))
			return &masks[i];
	return NULL;
}

outfit_t* find_outfit(lstr_t slug) {
	for (usz i = 0; i < outfit_count; ++i)
		if (lt_lstr_eq(slug, outfits[i].slug))
			return &outfits[i];
	return NULL;
}

i8 find_chest_index(lstr_t slug) {
	for (usz i = 0; i < chest_count; ++i)
		if (lt_lstr_eq(slug, chests[i].slug))
			return i;
	return -1;
}

i8 find_bank_index(lstr_t slug) {
	for (usz i = 0; i < bank_count; ++i)
		if (lt_lstr_eq(slug, banks[i].slug))
			return i;
	return -1;
}

i8 find_npc_index(lstr_t slug) {
	for (usz i = 0; i < npc_count; ++i)
		if (lt_lstr_eq(slug, npcs[i].slug))
			return i;
	return -1;
}

u8 find_direction(lstr_t str) {
	if (lt_lstr_eq(str, CLSTR("up")))
		return DIR_UP;
	if (lt_lstr_eq(str, CLSTR("down")))
		return DIR_DOWN;
	if (lt_lstr_eq(str, CLSTR("left")))
		return DIR_LEFT;
	if (lt_lstr_eq(str, CLSTR("right")))
		return DIR_RIGHT;
	return DIR_DOWN;
}

lstr_t asprintf(lt_arena_t* arena, char* fmt, ...) {
	va_list list;
	va_start(list, fmt);
	char* str = lt_arena_reserve(arena, 0);
	isz len = lt_str_vprintf(str, fmt, list);
	lt_arena_reserve(arena, len);
	va_end(list);
	return LSTR(str, len);
}

chest_t* chest_add(lt_arena_t* arena, lt_json_t* json) {
	usz pfx_len = CLSTR("Chest|").len;
	chests[chest_count].slug = LSTR(json->key.str + pfx_len, json->key.len - pfx_len);
	chests[chest_count].opened_at = -1;

	lstr_t img_slug = lt_json_find_child(json, CLSTR("imageSourceSlug"))->str_val;
	res_load_texture(arena, img_slug, &chests[chest_count].texture);

	return &chests[chest_count++];
}

bank_t* bank_add(lt_arena_t* arena, lt_json_t* json) {
	usz pfx_len = CLSTR("Bank|").len;
	banks[bank_count].slug = LSTR(json->key.str + pfx_len, json->key.len - pfx_len);
	banks[bank_count].opened_at = -1;
	banks[bank_count].closed_at = -1;

	lstr_t img_slug = lt_json_find_child(json, CLSTR("imageSourceSlug"))->str_val;
	res_load_texture(arena, img_slug, &banks[bank_count].texture);

	return &banks[bank_count++];
}

npc_t* npc_add(lt_arena_t* arena, lt_json_t* json) {
	usz pfx_len = CLSTR("NPC|").len;
	lstr_t slug = LSTR(json->key.str + pfx_len, json->key.len - pfx_len);
	npcs[npc_count].slug = slug;
	npcs[npc_count].name = lt_json_find_child(json, CLSTR("name"))->str_val;
	npcs[npc_count].direction = find_direction(lt_json_find_child(json, CLSTR("direction"))->str_val);

	lstr_t img_slug = asprintf(arena, "npcs/%S", slug);
	res_load_texture(arena, img_slug, &npcs[npc_count].texture);

	img_slug = lt_json_find_child(json, CLSTR("indicatorImageSourceSlug"))->str_val;
	res_load_texture(arena, img_slug, &npcs[npc_count].indicator_texture);

	return &npcs[npc_count++];
}

mask_t* mask_add(lt_arena_t* arena, lt_json_t* json) {
	usz pfx_len = CLSTR("Mask|").len;
	masks[mask_count].slug = LSTR(json->key.str + pfx_len, json->key.len - pfx_len);

	lstr_t cosmetic_slug = lt_json_find_child(json, CLSTR("headCosmeticSlug"))->str_val;

	lstr_t img_slug = asprintf(arena, "heads/%S/front/masculine", cosmetic_slug);
	res_load_texture(arena, img_slug, &masks[mask_count].texture_m);

	img_slug = asprintf(arena, "heads/%S/front/feminine", cosmetic_slug);
	res_load_texture(arena, img_slug, &masks[mask_count].texture_f);

	return &masks[mask_count++];
}

outfit_t* outfit_add(lt_arena_t* arena, lt_json_t* json) {
	usz pfx_len = CLSTR("Outfit|").len;
	outfits[outfit_count].slug = LSTR(json->key.str + pfx_len, json->key.len - pfx_len);

	lstr_t cosmetic_slug = lt_json_find_child(json, CLSTR("bodyCosmeticSlug"))->str_val;

	lstr_t img_slug = asprintf(arena, "bodies/%S/masculine", cosmetic_slug);
	res_load_texture(arena, img_slug, &outfits[outfit_count].texture_m);

	img_slug = asprintf(arena, "bodies/%S/feminine", cosmetic_slug);
	res_load_texture(arena, img_slug, &outfits[outfit_count].texture_f);

	return &outfits[outfit_count++];
}

tileset_t* tileset_add(lt_arena_t* arena, lt_json_t* json) {
	lt_json_t* tiles = lt_json_find_child(json, CLSTR("tiles"));

	usz cols = lt_json_uint_val(lt_json_find_child(json, CLSTR("width")));
	usz rows = tiles->child->child_count;
	LT_ASSERT(cols && rows);

	lt_json_t* it = tiles->child;
	LT_ASSERT(tiles->child_count == cols);

	usz tile_count = rows * cols;
	b8* collision = lt_arena_reserve(arena, tile_count * sizeof(b8));
	i8* chests = lt_arena_reserve(arena, tile_count * sizeof(i8));
	i8* banks = lt_arena_reserve(arena, tile_count * sizeof(i8));
	i8* npcs = lt_arena_reserve(arena, tile_count * sizeof(i8));

	usz x = 0;
	while (it) {
		lt_json_t* tile_it = it->child;
		usz y = 0;
		while (tile_it) {
			usz index = y * cols + x;
			collision[index] = lt_json_bool_val(lt_json_find_child(tile_it, CLSTR("collision")));

			lstr_t bankslug = lt_json_find_child(tile_it, CLSTR("bankSlug"))->str_val;
			lstr_t chestslug = lt_json_find_child(tile_it, CLSTR("chestSlug"))->str_val;
			lstr_t npcslug = lt_json_find_child(tile_it, CLSTR("npcSlug"))->str_val;

			if (chestslug.len)
				chests[index] = find_chest_index(chestslug);
			else
				chests[index] = -1;

			if (bankslug.len)
				banks[index] = find_bank_index(bankslug);
			else
				banks[index] = -1;

			if (npcslug.len)
				npcs[index] = find_npc_index(npcslug);
			else
				npcs[index] = -1;

			tile_it = tile_it->next;
			++y;
		}
		LT_ASSERT(y == rows);

		++x;
		it = it->next;
	}
	LT_ASSERT(x == cols);

	usz pfx_len = CLSTR("Tileset|").len;
	lstr_t name = LSTR(json->key.str + pfx_len, json->key.len - pfx_len);

	char buf[128];
	usz len = lt_str_printf(buf, "tilesets/%S", name);
	res_load_texture(arena, LSTR(buf, len), &tilesets[tileset_count].texture);

	tilesets[tileset_count].tile_count = tile_count;
	tilesets[tileset_count].width = cols;
	tilesets[tileset_count].height = rows;
	tilesets[tileset_count].collisions = collision;
	tilesets[tileset_count].chests = chests;
	tilesets[tileset_count].banks = banks;
	tilesets[tileset_count].npcs = npcs;
	tilesets[tileset_count].name = name;
	return &tilesets[tileset_count++];
}

tileset_t* tilemap_lookup_index(tilemap_t* tilemap, u16* tile_ptr) {
	u16 tile = *tile_ptr;

	for (usz i = 0; i < tilemap->tileset_count; ++i) {
		usz start = tilemap->tileset_start_indices[i];
		if (tile >= start && tile < start + tilemap->tilesets[i]->tile_count) {
			*tile_ptr -= start;
			return tilemap->tilesets[i];
		}
	}
	return NULL;
}

tilemap_t* tilemap_add(lt_arena_t* arena, lt_json_t* json) {
	lt_json_t* tiles_js = lt_json_find_child(json, CLSTR("tiles"));

	usz h = tiles_js->child_count;
	usz w = tiles_js->child->child_count;
	LT_ASSERT(w && h);

	usz tile_count = h * w;

	u16* a_tiles = lt_arena_reserve(arena, tile_count * 2 * sizeof(u16));
	u16* b_tiles = lt_arena_reserve(arena, tile_count * 2 * sizeof(u16));
	i16* chests = lt_arena_reserve(arena, tile_count * sizeof(i16));
	i16* banks = lt_arena_reserve(arena, tile_count * sizeof(i16));
	i16* npcs = lt_arena_reserve(arena, tile_count * sizeof(i16));
	u16* tiles = lt_arena_reserve(arena, 0);

	u16 tile_it = 0, tile_index = 0;

	LT_ASSERT(tiles_js->child_count == h);
	for (lt_json_t* i = tiles_js->child; i; i = i->next) {
		LT_ASSERT(i->child_count == w);
		for (lt_json_t* j = i->child; j; j = j->next) {
			a_tiles[tile_it] = tile_index;
			for (lt_json_t* a_it = lt_json_find_child(j, CLSTR("aboveIndices"))->child; a_it; a_it = a_it->next)
				tiles[tile_index++] = lt_json_uint_val(a_it);
			a_tiles[tile_it + 1] = tile_index - a_tiles[tile_it];

			b_tiles[tile_it] = tile_index;
			for (lt_json_t* b_it = lt_json_find_child(j, CLSTR("belowIndices"))->child; b_it; b_it = b_it->next)
				tiles[tile_index++] = lt_json_uint_val(b_it);
			b_tiles[tile_it + 1] = tile_index - b_tiles[tile_it];

			lt_json_t* chest = lt_json_find_child(j, CLSTR("chestIndex"));
			lt_json_t* bank = lt_json_find_child(j, CLSTR("bankIndex"));
			lt_json_t* npc = lt_json_find_child(j, CLSTR("npcIndex"));

			if (chest->stype != LT_JSON_NULL)
				chests[tile_it / 2] = lt_json_uint_val(chest);
			else
				chests[tile_it / 2] = -1;

			if (bank->stype != LT_JSON_NULL)
				banks[tile_it / 2] = lt_json_uint_val(bank);
			else
				banks[tile_it / 2] = -1;

			if (npc->stype != LT_JSON_NULL)
				npcs[tile_it / 2] = lt_json_uint_val(npc);
			else
				npcs[tile_it / 2] = -1;

			tile_it += 2;
		}
	}

	lt_arena_reserve(arena, tile_index * sizeof(u16));

	lt_json_t* sets_js = lt_json_find_child(json, CLSTR("tilesets"));
	usz set_count = sets_js->child_count;
	tileset_t** sets = lt_arena_reserve(arena, set_count * sizeof(tileset_t*));
	u16* set_start_indices = lt_arena_reserve(arena, set_count * sizeof(u16));

	usz set_i = 0;
	for (lt_json_t* set_it = sets_js->child; set_it; set_it = set_it->next) {
		sets[set_i] = find_tileset(lt_json_find_child(set_it, CLSTR("tileset"))->str_val);
		set_start_indices[set_i++] = lt_json_uint_val(lt_json_find_child(set_it, CLSTR("firstTileID")));
	}

	tilemaps[tilemap_count].w = w;
	tilemaps[tilemap_count].h = h;
	tilemaps[tilemap_count].tiles = tiles;
	tilemaps[tilemap_count].a_tile_indices = a_tiles;
	tilemaps[tilemap_count].b_tile_indices = b_tiles;

	tilemaps[tilemap_count].tileset_start_indices = set_start_indices;
	tilemaps[tilemap_count].tilesets = sets;
	tilemaps[tilemap_count].tileset_count = tileset_count;
	tilemaps[tilemap_count].chest_indices = chests;
	tilemaps[tilemap_count].bank_indices = banks;
	tilemaps[tilemap_count].npc_indices = npcs;

	usz pfx_len = CLSTR("Tilemap|").len;
	tilemaps[tilemap_count].name = LSTR(json->key.str + pfx_len, json->key.len - pfx_len);

	return &tilemaps[tilemap_count++];
}

#define CMD_SWITCH_CHAR 1
#define CMD_GET_CHARS	2
u8 cmd = CMD_GET_CHARS;
u8 cmd_charid = 0;
int cmd_start_page = -1;
b8 cmd_pages_wrapped = 0;

u8 state_switch_sent = 0;

#define RETRO_CHARSEL	0
#define RETRO_WORLD		1
#define RETRO_INVENTORY	2
#define RETRO_SPELLBOOK	3
#define RETRO_STATS		4

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

void send_key_down(lt_arena_t* arena, char key) {
	char* msg_buf = lt_arena_reserve(arena, 0);
	usz msg_len = lt_str_printf(msg_buf, "42[\"keydown\",\"%c\"]", key);
	ws_send_text(sock, LSTR(msg_buf, msg_len));
// 	lt_printf("%S\n", LSTR(msg_buf, msg_len));
}

void send_key_up(lt_arena_t* arena, char key) {
	char* msg_buf = lt_arena_reserve(arena, 0);
	usz msg_len = lt_str_printf(msg_buf, "42[\"keyup\",\"%c\"]", key);
	ws_send_text(sock, LSTR(msg_buf, msg_len));
// 	lt_printf("%S\n", LSTR(msg_buf, msg_len));
}

void send_key(lt_arena_t* arena, char key) {
	send_key_down(arena, key);
	send_key_up(arena, key);
}

#include <lt/time.h>
#include <stdlib.h>
#include <math.h>

void on_msg(lt_arena_t* arena, lt_socket_t* sock, lt_json_t* it) {
	if (lt_lstr_eq(it->str_val, CLSTR("play"))) {
		lt_printf("Successfully connected to '%s'\n", HOST);
	}
	else if (lt_lstr_eq(it->str_val, CLSTR("update"))) {
// 		static u64 ltime = 0;
// 		u64 time = lt_hfreq_time_msec();
// 		lt_printf("Update %udms\n", time - ltime);
// 		ltime = time;

		lt_mutex_lock(state_lock);

		lt_json_t* chats = lt_json_find_child(it->next, CLSTR("chats"));
		lt_json_t* chat_it = chats->child;
		while (chat_it) {
			lstr_t type = lt_json_find_child(chat_it, CLSTR("type"))->str_val;
			lstr_t player_slug = lt_json_find_child(chat_it, CLSTR("playerSlug"))->str_val;

			if (lt_lstr_eq(type, CLSTR("logout"))) {
				lstr_t username = find_player_from_slug(player_slug)->username;
				lstr_t msg_str = asprintf(arena, "%S has logged out", username);

				chat_msgs[chat_msg_count].str = malloc(msg_str.len);
				chat_msgs[chat_msg_count].len = msg_str.len;
				memcpy(chat_msgs[chat_msg_count].str, msg_str.str, msg_str.len);
				chat_msg_count++;
			}

			chat_it = chat_it->next;
		}

		lstr_t local_player_slug = lt_json_find_child(it->next, CLSTR("playerSlug"))->str_val;

		lt_json_t* pieces = lt_json_find_child(it->next, CLSTR("pieces"));
		lt_json_t* piece_it = pieces->child;
		player_count = 0;

		lt_json_t* logout = lt_json_find_child(pieces, CLSTR("Switch|picture/world/logout"));

		lt_json_t* botbar_inv = lt_json_find_child(pieces, CLSTR("Picture|world/bottom-bar/icons/inventory"));
		lt_json_t* botbar_spellb = lt_json_find_child(pieces, CLSTR("Picture|world/bottom-bar/icons/spellbook"));
		lt_json_t* botbar_stats = lt_json_find_child(pieces, CLSTR("Picture|world/bottom-bar/icons/stats"));

		u8 lstate = retro_state;

		if (lt_json_find_child(pieces, CLSTR("Label|character-select/title")))
			retro_state = RETRO_CHARSEL;
		else if (logout) {
			lstr_t inv_img_slug = lt_json_find_child(botbar_inv, CLSTR("imageSourceSlug"))->str_val;
			lstr_t spellb_img_slug = lt_json_find_child(botbar_spellb, CLSTR("imageSourceSlug"))->str_val;
			lstr_t stats_img_slug = lt_json_find_child(botbar_stats, CLSTR("imageSourceSlug"))->str_val;

			if (lt_lstr_eq(inv_img_slug, CLSTR("bottom-bar-icons/inventory/selected")))
				retro_state = RETRO_INVENTORY;
			else if (lt_lstr_eq(spellb_img_slug, CLSTR("bottom-bar-icons/spellbook/selected")))
				retro_state = RETRO_SPELLBOOK;
			else if (lt_lstr_eq(stats_img_slug, CLSTR("bottom-bar-icons/stats/selected")))
				retro_state = RETRO_STATS;
			else
				retro_state = RETRO_WORLD;
		}
		else
			LT_ASSERT_NOT_REACHED();

// 		if (lstate != retro_state) {
// 			if (retro_state == RETRO_WORLD || retro_state == RETRO_STATS)
// 				send_key(arena, 'c');
// 			else if (retro_state == RETRO_INVENTORY)
// 				send_key(arena, 'x');
// 			else if (retro_state == RETRO_SPELLBOOK)
// 				send_key(arena, 'z');
// 		}

// 		lt_json_print(lt_stdout, it->next);

		static int last_pageid = -1;
		static b8 awaiting_pageswitch = 0;
		u32 charsel_pageid = 0;

		if (retro_state == RETRO_CHARSEL) {
			lt_json_t* rarrow = lt_json_find_child(pieces, CLSTR("Switch|picture/character-customize/page/left"));
			lt_json_t* page = lt_json_find_child(pieces, CLSTR("Label|character-select/page"));

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
				usz key_len = lt_str_printf(key_buf, "Switch|picture/character-select/character/%ud/play", cmd_charid % 7);
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
		else if (logout) {
			if (cmd == CMD_SWITCH_CHAR || cmd == CMD_GET_CHARS) {
				int x = lt_json_int_val(lt_json_find_child(logout, CLSTR("x")));
				int y = lt_json_int_val(lt_json_find_child(logout, CLSTR("y")));

				send_click(arena, x + 1, y + 1);
			}
		}

		while (piece_it) {
			if (lt_lstr_startswith(piece_it->key, CLSTR("Player|"))) {
				usz pfx_len = CLSTR("Player|").len;

				lstr_t slug = LSTR(piece_it->key.str + pfx_len, piece_it->key.len - pfx_len);
				lstr_t username = lt_json_find_child(piece_it, CLSTR("username"))->str_val;

				memcpy(player_slugs[player_count], slug.str, slug.len);
				memcpy(player_usernames[player_count], username.str, username.len);

				players[player_count].username = LSTR(player_usernames[player_count], username.len);
				players[player_count].slug = LSTR(player_slugs[player_count], slug.len);

				lt_json_t* tilemap_js = lt_json_find_child(piece_it, CLSTR("tilemapSlug"));
				if (tilemap_js->stype == LT_JSON_STRING)
					players[player_count].tilemap = find_tilemap(tilemap_js->str_val);
				else
					players[player_count].tilemap = NULL;

				if (lt_lstr_eq(slug, local_player_slug))
					local_playerid = player_count;

				lt_json_t* x_js = lt_json_find_child(piece_it, CLSTR("x"));
				lt_json_t* y_js = lt_json_find_child(piece_it, CLSTR("y"));
				if (x_js->stype != LT_JSON_NULL && y_js->stype != LT_JSON_NULL) {
					players[player_count].x = lt_json_int_val(x_js);
					players[player_count].y = lt_json_int_val(y_js);
				}

				lt_json_t* dir_js = lt_json_find_child(piece_it, CLSTR("direction"));
				if (dir_js->stype != LT_JSON_NULL)
					players[player_count].direction = find_direction(dir_js->str_val);

				lt_json_t* mask_js = lt_json_find_child(piece_it, CLSTR("maskItemSlug"));
				if (mask_js->stype != LT_JSON_NULL)
					players[player_count].mask = find_mask(mask_js->str_val);
				else
					players[player_count].mask = NULL;

				lt_json_t* outfit_js = lt_json_find_child(piece_it, CLSTR("outfitItemSlug"));
				if (outfit_js->stype != LT_JSON_NULL)
					players[player_count].outfit = find_outfit(outfit_js->str_val);
				else
					players[player_count].outfit = NULL;

				lt_json_t* wstart_js = lt_json_find_child(piece_it, CLSTR("sinceWalkAnimationStarted"));
				if (wstart_js->stype != LT_JSON_NULL)
					players[player_count].walk_start = lt_json_int_val(wstart_js);
				else
					players[player_count].walk_start = MOVESPEED;

				player_count++;
			}
			else if (cmd == CMD_GET_CHARS && lt_lstr_startswith(piece_it->key, CLSTR("Label|character-select/character/"))) {
				usz pfx_len = CLSTR("Label|character-select/character/").len;
				usz char_id = lt_lstr_uint(LSTR(piece_it->key.str + pfx_len, piece_it->key.len - pfx_len));

				char_id += charsel_pageid * 7;

				if (char_id + 1 > character_count)
					character_count = char_id + 1;

				lstr_t name = lt_json_find_child(piece_it, CLSTR("text"))->str_val;

				char* lv_begin = name.str + CLSTR("Lv").len, *lv_it = lv_begin;
				while (lt_is_digit(*lv_it))
					++lv_it;
				characters[char_id].level = lt_lstr_uint(LSTR(lv_begin, lv_it - lv_begin));
				if (lt_lstr_endswith(name, CLSTR("WR")))
					characters[char_id].class_name = CLSTR("Warrior");
				else if (lt_lstr_endswith(name, CLSTR("WZ")))
					characters[char_id].class_name = CLSTR("Wizard");
				else if (lt_lstr_endswith(name, CLSTR("CL")))
					characters[char_id].class_name = CLSTR("Cleric");
				characters[char_id].description.str = character_descriptions[char_id];
				characters[char_id].description.len = lt_str_printf(character_descriptions[char_id], "%ud %S", char_id + 1, characters[char_id].class_name);
				characters[char_id].page = charsel_pageid;
				characters[char_id].present = 1;
			}
			else if (retro_state == RETRO_INVENTORY && lt_lstr_startswith(piece_it->key, CLSTR("Label|world/inventory/bag/"))) {
				usz pfx_len = CLSTR("Label|world/inventory/bag/").len;
				usz slot_id = lt_lstr_uint(LSTR(piece_it->key.str + pfx_len, piece_it->key.len - pfx_len));

				if (slot_id + 1 > inv_item_count)
					inv_item_count = slot_id + 1;

				lstr_t name = lt_json_find_child(piece_it, CLSTR("text"))->str_val;
				inv_items[slot_id].str = inv_item_names[slot_id];
				inv_items[slot_id].len = name.len;
				memcpy(inv_item_names[slot_id], name.str, name.len);
			}
			else if (lt_lstr_startswith(piece_it->key, CLSTR("Chest|"))) {
				usz pfx_len = CLSTR("Chest|").len;
				lstr_t slug = LSTR(piece_it->key.str + pfx_len, piece_it->key.len - pfx_len);

				isz chest_index = find_chest_index(slug);
				LT_ASSERT(chest_index >= 0);

				lt_json_t* opened_at = lt_json_find_child(piece_it, CLSTR("openedAt"));
				if (opened_at->stype != LT_JSON_NULL)
					chests[chest_index].opened_at = lt_json_int_val(opened_at);;
			}
			else if (lt_lstr_startswith(piece_it->key, CLSTR("NPC|"))) {
				usz pfx_len = CLSTR("NPC|").len;
				lstr_t slug = LSTR(piece_it->key.str + pfx_len, piece_it->key.len - pfx_len);

				isz npc_index = find_npc_index(slug);
				LT_ASSERT(npc_index >= 0);

				lt_json_t* dir_js = lt_json_find_child(piece_it, CLSTR("direction"));
				if (dir_js->stype != LT_JSON_NULL)
					npcs[npc_index].direction = find_direction(dir_js->str_val);
			}
			piece_it = piece_it->next;
		}

		chat_it = chats->child;
		while (chat_it) {
			lstr_t type = lt_json_find_child(chat_it, CLSTR("type"))->str_val;
			lstr_t player_slug = lt_json_find_child(chat_it, CLSTR("playerSlug"))->str_val;

			lstr_t msg_str = NLSTR();
			if (lt_lstr_eq(type, CLSTR("message"))) {
				lstr_t username = find_player_from_slug(player_slug)->username;
				lstr_t msg = lt_json_find_child(chat_it, CLSTR("contents"))->str_val;
// 				lstr_t channel = lt_json_find_child(chat_it, CLSTR("channel"))->str_val;
				msg_str = asprintf(arena, "[%S] %S", username, msg);
			}
			else if (lt_lstr_eq(type, CLSTR("login"))) {
				lstr_t username = find_player_from_slug(player_slug)->username;
				msg_str = asprintf(arena, "%S has logged in", username);
			}

			if (msg_str.len) {
				chat_msgs[chat_msg_count].str = malloc(msg_str.len);
				chat_msgs[chat_msg_count].len = msg_str.len;
				memcpy(chat_msgs[chat_msg_count].str, msg_str.str, msg_str.len);
				chat_msg_count++;
			}

			chat_it = chat_it->next;
		}

		if (retro_state == RETRO_INVENTORY) {
			lt_json_t* gold_json = lt_json_find_child(pieces, CLSTR("Label|world/inventory/gold"));
			lstr_t gold_str = lt_json_find_child(gold_json, CLSTR("text"))->str_val;
			gold.str = gold_buf;
			gold.len = gold_str.len;
			memcpy(gold_buf, gold_str.str, gold_str.len);
		}

		last_pageid = charsel_pageid;

		lt_mutex_release(state_lock);
	}
	else {
		lt_werrf("Unknown or invalid message type '%S'\n", it->str_val);
	}
}

void load_texture(char* path, int* id) {
	lt_arestore_t arestore = lt_arena_save(arena);

	lstr_t tex_file;
	if (!lt_file_read_entire(arena, path, &tex_file))
		lt_ferrf("Failed to open %s\n", path);

	lt_img_t img;
	if (!lt_img_load_tga(arena, tex_file.str, tex_file.len, &img))
		lt_ferrf("Failed to load %s\n", path);

	render_create_tex(img.width, img.height, img.data, id, TEXTURE_FILTER);

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
				lt_printf("Connection closed by host\n");
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

void draw_npc(float scr_x, float scr_y, float scr_tilew, int tex, u8 dir) {
	glBindTexture(GL_TEXTURE_2D, tex);

	float uv_x = 0.0f;
	float uv_y = 0.25f * dir;

	glBegin(GL_QUADS);
	glTexCoord2f(uv_x, uv_y); glVertex2f(scr_x, scr_y);
	glTexCoord2f(uv_x + 0.25f, uv_y); glVertex2f(scr_x + scr_tilew, scr_y);
	glTexCoord2f(uv_x + 0.25f, uv_y + 0.25f); glVertex2f(scr_x + scr_tilew, scr_y + scr_tilew);
	glTexCoord2f(uv_x, uv_y + 0.25f); glVertex2f(scr_x, scr_y + scr_tilew);
	glEnd();
}

void draw_cosmetic(float scr_x, float scr_y, float scr_tilew, int tex, u8 dir, usz frame) {
	glBindTexture(GL_TEXTURE_2D, tex);

	float uv_x = 0.25f * frame;
	float uv_y = 0.25f * dir;

	glBegin(GL_QUADS);
	glTexCoord2f(uv_x, uv_y); glVertex2f(scr_x, scr_y);
	glTexCoord2f(uv_x + 0.25f, uv_y); glVertex2f(scr_x + scr_tilew, scr_y);
	glTexCoord2f(uv_x + 0.25f, uv_y + 0.25f); glVertex2f(scr_x + scr_tilew, scr_y + scr_tilew);
	glTexCoord2f(uv_x, uv_y + 0.25f); glVertex2f(scr_x, scr_y + scr_tilew);
	glEnd();
}

void draw_tile(tilemap_t* tilemap, float scr_x, float scr_y, float scr_tilew, u16 tile) {
	tileset_t* tileset = tilemap_lookup_index(tilemap, &tile);
	LT_ASSERT(tileset);

	glBindTexture(GL_TEXTURE_2D, tileset->texture);

	float ts_tilew = 1.0f / tileset->width;
	float ts_tileh = 1.0f / tileset->height;
	float ts_x = (tile % tileset->width) * ts_tilew;
	float ts_y = (tile / tileset->width) * ts_tileh;

	glBegin(GL_QUADS);
	glTexCoord2f(ts_x, ts_y); glVertex2f(scr_x, scr_y);
	glTexCoord2f(ts_x + ts_tilew, ts_y); glVertex2f(scr_x + scr_tilew, scr_y);
	glTexCoord2f(ts_x + ts_tilew, ts_y + ts_tileh); glVertex2f(scr_x + scr_tilew, scr_y + scr_tilew);
	glTexCoord2f(ts_x, ts_y + ts_tileh); glVertex2f(scr_x, scr_y + scr_tilew);
	glEnd();
}

void draw_sprite(float x, float y, float w, float h, int tex) {
	glBindTexture(GL_TEXTURE_2D, tex);

	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 0.0f); glVertex2f(x, y);
	glTexCoord2f(1.0f, 0.0f); glVertex2f(x + w, y);
	glTexCoord2f(1.0f, 1.0f); glVertex2f(x + w, y + h);
	glTexCoord2f(0.0f, 1.0f); glVertex2f(x, y + h);
	glEnd();
}

b8 collide_at(tilemap_t* tilemap, isz x, isz y) {
	if (x < 0 || y < 0 || x >= tilemap->w || y >= tilemap->h)
		return 1;

	usz ii = x * tilemap->w + y;

	u16 tile_index = tilemap->b_tile_indices[ii * 2];
	u16 tile_count = tilemap->b_tile_indices[ii * 2 + 1];

	for (usz ti = 0; ti < tile_count; ++ti) {
		u16 tile = tilemap->tiles[tile_index + ti];
		tileset_t* tileset = tilemap_lookup_index(tilemap, &tile);
		LT_ASSERT(tileset);
		if (tileset->collisions[tile])
			return 1;
	}

	if (tilemap->npc_indices[ii] != -1)
		return 1;
	if (tilemap->chest_indices[ii] != -1)
		return 1;
	if (tilemap->bank_indices[ii] != -1)
		return 1;

	return 0;
}

int main(int argc, char** argv) {
	arena = lt_arena_alloc(LT_MB(16));

	state_lock = lt_mutex_create(arena);

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
	lstr_t ddef_html;
	if (!res_load(arena, CLSTR("/"), &ddef_html))
		lt_ferrf("Failed to load '/'\n");

	// Find beginning of data-definitions attribute
	lstr_t ddef_signature = CLSTR("data-definitions=\"");
	char* defs_start = NULL;
	char* defs_it = ddef_html.str, *html_end = ddef_html.str + ddef_html.len - ddef_signature.len;
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

	lt_json_t* ddefs_js = lt_json_parse(arena, ddefs, ddefs_len);

	lt_json_t* ddef_it = ddefs_js->child;
	while (ddef_it) {
		if (lt_lstr_startswith(ddef_it->key, CLSTR("Chest|")))
			chest_add(arena, ddef_it);
		else if (lt_lstr_startswith(ddef_it->key, CLSTR("Bank|")))
			bank_add(arena, ddef_it);
		else if (lt_lstr_startswith(ddef_it->key, CLSTR("NPC|")))
			npc_add(arena, ddef_it);
		else if (lt_lstr_startswith(ddef_it->key, CLSTR("Mask|")))
			mask_add(arena, ddef_it);
		else if (lt_lstr_startswith(ddef_it->key, CLSTR("Outfit|")))
			outfit_add(arena, ddef_it);
		ddef_it = ddef_it->next;
	}

	ddef_it = ddefs_js->child;
	while (ddef_it) {
		if (lt_lstr_startswith(ddef_it->key, CLSTR("Tileset|")))
			tileset_add(arena, ddef_it);
		ddef_it = ddef_it->next;
	}

	ddef_it = ddefs_js->child;
	while (ddef_it) {
		if (lt_lstr_startswith(ddef_it->key, CLSTR("Tilemap|")))
			tilemap_add(arena, ddef_it);
		ddef_it = ddef_it->next;
	}

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
	render_create_tex(font->width * gcount, font->height, glyph_bm_buf, &glyph_bm, TEXTURE_FILTER);

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

	lt_arena_t* arena = lt_arena_alloc(LT_MB(1));

	while (!lt_window_closed(win) && !quit) {
		lt_arestore_t arestore = lt_arena_save(arena);

		lt_window_event_t evs[16];
		usz ev_count = lt_window_poll_events(win, evs, 16);

		lt_window_mouse_pos(win, &cx->mouse_x, &cx->mouse_y);
		cx->mouse_state = lt_window_key_pressed(win, LT_KEY_MB1);

		render_begin(win);

		int width, height;
		lt_window_get_size(win, &width, &height);

		lt_mutex_lock(state_lock);
		tilemap_t* tilemap = players[local_playerid].tilemap;

		u64 time_msec = lt_hfreq_time_msec();

		static i64 w_pressed_msec = -1;
		static i64 a_pressed_msec = -1;
		static i64 s_pressed_msec = -1;
		static i64 d_pressed_msec = -1;

		static isz predict_x = 0;
		static isz predict_y = 0;

		for (usz i = 0; i < ev_count; ++i) {
			lt_window_event_t ev = evs[i];

			switch (ev.type) {
			case LT_WIN_EVENT_KEY_PRESS:
				if (ev.key == LT_KEY_W)
					w_pressed_msec = time_msec;
				else if (ev.key == LT_KEY_A)
					a_pressed_msec = time_msec;
				else if (ev.key == LT_KEY_S)
					s_pressed_msec = time_msec;
				else if (ev.key == LT_KEY_D)
					d_pressed_msec = time_msec;
				break;

			case LT_WIN_EVENT_KEY_RELEASE:
				if (ev.key == LT_KEY_W)
					w_pressed_msec = -1;
				else if (ev.key == LT_KEY_A)
					a_pressed_msec = -1;
				else if (ev.key == LT_KEY_S)
					s_pressed_msec = -1;
				else if (ev.key == LT_KEY_D)
					d_pressed_msec = -1;
				break;

			default:
				break;
			}
		}

		static i8 predict_move_dir = -1;
		static u64 walk_start_msec = 0;

		i8 move_dir = -1;
		u64 move_pressed_msec = 0;
		if (w_pressed_msec != -1) {
			move_dir = DIR_UP;
			move_pressed_msec = w_pressed_msec;
		}
		if (a_pressed_msec != -1 && a_pressed_msec > move_pressed_msec) {
			move_dir = DIR_LEFT;
			move_pressed_msec = a_pressed_msec;
		}
		if (s_pressed_msec != -1 && s_pressed_msec > move_pressed_msec) {
			move_dir = DIR_DOWN;
			move_pressed_msec = s_pressed_msec;
		}
		if (d_pressed_msec != -1 && d_pressed_msec > move_pressed_msec) {
			move_dir = DIR_RIGHT;
			move_pressed_msec = d_pressed_msec;
		}

		static u64 walk_anim_start_msec = 0;

		u64 walk_time_delta = time_msec - walk_start_msec;

		if (walk_time_delta > 125) { // Wait at least 125ms before sending the WASD keyups.
			send_key_up(arena, 'w'); // The server just ignores the entire key press otherwise.
			send_key_up(arena, 'a'); // (For some damn reason?)
			send_key_up(arena, 's');
			send_key_up(arena, 'd');
		}

		if (walk_time_delta > MOVESPEED) {
			if (move_dir != -1) {
				predict_move_dir = move_dir;

				switch (move_dir) {
				case DIR_UP:
					if (!collide_at(tilemap, predict_x, predict_y - 1)) {
						--predict_y;
						walk_anim_start_msec = time_msec;
						walk_start_msec = time_msec;
						send_key_down(arena, 'w');
					}
					break;
				case DIR_LEFT:
					if (!collide_at(tilemap, predict_x - 1, predict_y)) {
						--predict_x;
						walk_anim_start_msec = time_msec;
						walk_start_msec = time_msec;
						send_key_down(arena, 'a');
					}
					break;
				case DIR_DOWN:
					if (!collide_at(tilemap, predict_x, predict_y + 1)) {
						++predict_y;
						walk_anim_start_msec = time_msec;
						walk_start_msec = time_msec;
						send_key_down(arena, 's');
					}
					break;
				case DIR_RIGHT:
					if (!collide_at(tilemap, predict_x + 1, predict_y)) {
						++predict_x;
						walk_anim_start_msec = time_msec;
						walk_start_msec = time_msec;
						send_key_down(arena, 'd');
					}
					break;
				}
			}
			else {
				predict_move_dir = players[local_playerid].direction;
				predict_x = players[local_playerid].x;
				predict_y = players[local_playerid].y;
			}
		}

		float predict_step_len = 1.0f - ((float)(time_msec - walk_anim_start_msec) / MOVESPEED);

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
// 		lt_gui_label(cx, CLSTR("Inventory:"), 0);
// 		for (usz i = 0; i < inv_item_count; ++i) {
// 			lt_gui_row(cx, 2);
// 			lt_gui_label(cx, CLSTR(" - "), 0);
// 			lt_gui_label(cx, inv_items[i], 0);
// 		}
// 		lt_gui_label(cx, gold, 0);

		lt_gui_rect_t game_area = lt_gui_get_container(cx)->a;
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

		render_scissor(NULL, &game_area);

		if (tilemap && cmd != CMD_GET_CHARS && cmd != CMD_SWITCH_CHAR) {
			float scr_tilew = 32.0f;

			float scr_tileoffs_x = game_area.x - (predict_x * scr_tilew) + game_area.w/2 - scr_tilew/2;
			float scr_tileoffs_y = game_area.y - (predict_y * scr_tilew) + game_area.h/2 - scr_tilew/2;

			if (predict_step_len > 0.0f) {
				switch (predict_move_dir) {
				case DIR_UP: scr_tileoffs_y -= predict_step_len * scr_tilew; break;
				case DIR_DOWN: scr_tileoffs_y += predict_step_len * scr_tilew; break;
				case DIR_LEFT: scr_tileoffs_x -= predict_step_len * scr_tilew; break;
				case DIR_RIGHT: scr_tileoffs_x += predict_step_len * scr_tilew; break;
				}
			}

			glEnable(GL_TEXTURE_2D);
			for (usz y = 0; y < tilemap->h; ++y) {
				for (usz x = 0; x < tilemap->w; ++x) {
					usz ii = x * tilemap->w + y;
					u16 tile_index = tilemap->b_tile_indices[ii * 2];
					u16 tile_count = tilemap->b_tile_indices[ii * 2 + 1];

					float scr_x = scr_tileoffs_x + x * scr_tilew;
					float scr_y = scr_tileoffs_y + y * scr_tilew;

					for (usz ti = 0; ti < tile_count; ++ti) {
						u16 tile = tilemap->tiles[tile_index + ti];
						draw_tile(tilemap, scr_x, scr_y, scr_tilew, tile);
					}

					i16 chest_index = tilemap->chest_indices[ii];
					if (chest_index >= 0) {
						tileset_t* ts = tilemap_lookup_index(tilemap, &chest_index);
						chest_t* chest = &chests[ts->chests[chest_index]];

						glBindTexture(GL_TEXTURE_2D, chest->texture);

						float uv_x = 0.0f;

						if (chest->opened_at != -1)
							uv_x += 0.5f;

						glBegin(GL_QUADS);
						glTexCoord2f(uv_x, 0.0f); glVertex2f(scr_x, scr_y);
						glTexCoord2f(0.25f, 0.0f); glVertex2f(scr_x + scr_tilew, scr_y);
						glTexCoord2f(0.25f, 1.0f); glVertex2f(scr_x + scr_tilew, scr_y + scr_tilew);
						glTexCoord2f(uv_x, 1.0f); glVertex2f(scr_x, scr_y + scr_tilew);
						glEnd();
					}

					i16 bank_index = tilemap->bank_indices[ii];
					if (bank_index >= 0) {
						tileset_t* ts = tilemap_lookup_index(tilemap, &bank_index);
						bank_t* bank = &banks[ts->banks[bank_index]];

						glBindTexture(GL_TEXTURE_2D, bank->texture);

						glBegin(GL_QUADS);
						glTexCoord2f(0.0f, 0.0f); glVertex2f(scr_x, scr_y);
						glTexCoord2f(0.25f, 0.0f); glVertex2f(scr_x + scr_tilew, scr_y);
						glTexCoord2f(0.25f, 1.0f); glVertex2f(scr_x + scr_tilew, scr_y + scr_tilew);
						glTexCoord2f(0.0f, 1.0f); glVertex2f(scr_x, scr_y + scr_tilew);
						glEnd();
					}

					i16 npc_index = tilemap->npc_indices[ii];
					if (npc_index >= 0) {
						tileset_t* ts = tilemap_lookup_index(tilemap, &npc_index);
						npc_t* npc = &npcs[ts->npcs[npc_index]];

						draw_npc(scr_x, scr_y, scr_tilew, npc->texture, npc->direction);
						draw_sprite(scr_x, scr_y - scr_tilew, scr_tilew, scr_tilew, npc->indicator_texture);
					}
				}
			}

			lt_gui_point_t* pname_pts = lt_arena_reserve(arena, player_count * sizeof(lt_gui_point_t));
			lstr_t* pname_strs = lt_arena_reserve(arena, player_count * sizeof(lstr_t));
			u32* pname_clrs = lt_arena_reserve(arena, player_count * sizeof(u32));
			usz pname_count = 0;

			lt_gui_rect_t* pname_bg_rects = lt_arena_reserve(arena, player_count * sizeof(lt_gui_rect_t));
			u32* pname_bg_clrs = lt_arena_reserve(arena, player_count * sizeof(u32));

			for (usz i = 0; i < player_count; ++i) {
				if (players[i].tilemap != tilemap)
					continue;

				float step_len = 1.0f - ((float)players[i].walk_start / MOVESPEED);

				isz x = players[i].x;
				isz y = players[i].y;
				u8 dir = players[i].direction;
				if (i == local_playerid) {
					dir = predict_move_dir;
					x = predict_x;
					y = predict_y;

					step_len = predict_step_len;
				}

				float scr_x = scr_tileoffs_x + x * scr_tilew;
				float scr_y = scr_tileoffs_y + y * scr_tilew;

				usz animation_frame = 0;

				if (step_len > 0.0f) {
					switch (dir) {
					case DIR_UP: scr_y += step_len * scr_tilew; break;
					case DIR_DOWN: scr_y -= step_len * scr_tilew; break;
					case DIR_LEFT: scr_x += step_len * scr_tilew; break;
					case DIR_RIGHT: scr_x -= step_len * scr_tilew; break;
					}
					animation_frame = ((lt_hfreq_time_msec() / 167) % 4);
				}

				outfit_t* outfit = players[i].outfit;
				if (outfit)
					draw_cosmetic(scr_x, scr_y, scr_tilew, outfit->texture_m, dir, animation_frame);
				mask_t* mask = players[i].mask;
				if (mask)
					draw_cosmetic(scr_x, scr_y, scr_tilew, mask->texture_m, dir, animation_frame);

				float name_w = font->width * players[i].username.len;
				float name_h = font->height;

				float name_x = scr_x + scr_tilew/2 - name_w/2;
				float name_y = scr_y - name_h;

				pname_pts[pname_count] = LT_GUI_POINT(round(name_x), round(name_y));
				pname_strs[pname_count] = players[i].username;
				pname_clrs[pname_count] = 0xFFFFFFFF;

				pname_bg_rects[pname_count] = LT_GUI_RECT(round(name_x - 2.0f), round(name_y - 1.0f), round(name_w + 4.0f), round(name_h + 2.0f));
				pname_bg_clrs[pname_count] = 0xFF000000;
				++pname_count;
			}

			for (usz y = 0; y < tilemap->h; ++y) {
				for (usz x = 0; x < tilemap->w; ++x) {
					usz ii = x * tilemap->w + y;
					u16 tile_index = tilemap->a_tile_indices[ii * 2];
					u16 tile_count = tilemap->a_tile_indices[ii * 2 + 1];

					float scr_x = scr_tileoffs_x + x * scr_tilew;
					float scr_y = scr_tileoffs_y + y * scr_tilew;

					for (usz ti = 0; ti < tile_count; ++ti) {
						u16 tile = tilemap->tiles[tile_index + ti];
						draw_tile(tilemap, scr_x, scr_y, scr_tilew, tile);
					}
				}
			}
			glDisable(GL_TEXTURE_2D);

			for (usz i = 0; i < pname_count; ++i) {
				render_draw_rect(NULL, 1, &pname_bg_rects[i], &pname_bg_clrs[i]);
				render_draw_text(NULL, 1, &pname_pts[i], &pname_strs[i], &pname_clrs[i]);
			}
		}

		lt_mutex_release(state_lock);

		render_end(win);

		lt_arena_restore(arena, &arestore);
	}

	lt_thread_join(recv_thread);

	lt_socket_destroy(sock);

	lt_window_destroy(win);

	lt_window_terminate();
}

