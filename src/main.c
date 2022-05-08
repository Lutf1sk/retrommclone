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
#include <lt/gl.h>

#include "websock.h"
#include "net_helpers.h"
#include "render.h"
#include "config.h"
#include "resource.h"
#include "map.h"
#include "send.h"
#include "chat.h"

#include "chest.h"
#include "npc.h"
#include "bank.h"

#include "cosmetic.h"

#include "game_helpers.h"

#include <time.h>

#define USERNAME_MAXLEN 18
#define USERSLUG_MAXLEN 21

typedef struct tilemap tilemap_t;
typedef struct player player_t;

typedef struct clothes_dye clothes_dye_t;
typedef struct hair_dye hair_dye_t;

lt_arena_t* arena = NULL;
lt_arena_t* render_arena = NULL;
lt_socket_t* sock = NULL;
lt_window_t* win = NULL;
lt_font_t* font = NULL;

int icons[LT_GUI_ICON_MAX];
int glyph_bm;

player_t* local_player = NULL;
b8 can_move = 0;

lstr_t dialogue_name = NLSTR();
lstr_t dialogue_text = NLSTR();

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
	clothes_dye_t* clothes_dye;
	hair_dye_t* hair_dye;

	u8 step_anim_offs;
	u64 walk_start;
} player_t;

#define MAX_PLAYERS 128

player_t players[MAX_PLAYERS];
int player_count = 0;

char player_usernames[MAX_PLAYERS][USERNAME_MAXLEN];
char player_slugs[MAX_PLAYERS][USERSLUG_MAXLEN];

#define MOVESPEED 250.0f

typedef
struct character {
	lstr_t class_name;
	lstr_t description;
	u8 page;
	u8 level;
	b8 present;
} character_t;

#define MAX_CHARACTERS 21

typedef
struct item {
	lstr_t slug;
	lstr_t name;
	lstr_t img_slug;
	void* dye;
	u16 sell_value;
	u8 flags;
} item_t;

#define ITEM_TRADABLE 1

item_t* items;
usz item_count = 0;

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

void send_pong(lt_socket_t* sock) {
	ws_send_text(sock, CLSTR("3"));
}

lt_mutex_t* state_lock;

player_t* find_player_from_slug(lstr_t slug) {
	for (usz i = 0; i < player_count; ++i)
		if (lt_lstr_eq(slug, players[i].slug))
			return &players[i];

	return NULL;
}

player_t* new_player(lstr_t slug, lstr_t username) {
	memset(&players[player_count], 0, sizeof(player_t));

	memcpy(player_slugs[player_count], slug.str, slug.len);
	players[player_count].slug = LSTR(player_slugs[player_count], slug.len);

	memcpy(player_usernames[player_count], username.str, username.len);
	players[player_count].username = LSTR(player_usernames[player_count], username.len);

	return &players[player_count++];
}

item_t* find_item(lstr_t slug) {
	for (usz i = 0; i < item_count; ++i)
		if (lt_lstr_eq(slug, items[i].slug))
			return &items[i];
	return NULL;
}

void item_add(lt_arena_t* arena, lt_json_t* json) {
	// TODO: Fix this garbage :P
	items = realloc(items, (item_count + 1) * sizeof(item_t));

	usz pfx_len = CLSTR("Item|").len;
	items[item_count].slug = LSTR(json->key.str + pfx_len, json->key.len - pfx_len);

	lt_json_t* clothes_dye_js = lt_json_find_child(json, CLSTR("clothesDyeSlug"));
	if (clothes_dye_js->stype != LT_JSON_NULL)
		items[item_count].dye = find_clothes_dye(clothes_dye_js->str_val);

	lt_json_t* hair_dye_js = lt_json_find_child(json, CLSTR("hairDyeSlug"));
	if (hair_dye_js->stype != LT_JSON_NULL)
		items[item_count].dye = find_hair_dye(hair_dye_js->str_val);

	item_count++;
}

#define CMD_SWITCH_CHAR 1
#define CMD_GET_CHARS	2
#define CMD_INN_ACCEPT	3
#define CMD_INN_REJECT	4
#define CMD_BANK_CLOSE	5
#define CMD_CREATE_CHAR 6

u8 cmd = CMD_GET_CHARS;
u8 cmd_charid = 0;
int cmd_start_page = -1;
b8 cmd_pages_wrapped = 0;

b8 popup_open = 0;

u8 state_switch_sent = 0;

#define RETRO_CHARSEL		0
#define RETRO_WORLD			1
#define RETRO_INVENTORY		2
#define RETRO_SPELLBOOK		3
#define RETRO_STATS			4
#define RETRO_DIALOGUE		5
#define RETRO_CHEST_REWARD	6
#define RETRO_BANK			7
#define RETRO_NPC_TRADE		8
#define RETRO_INN			9

lstr_t interaction_str = NLSTR();
char interaction_str_buf[64];

u8 retro_state;

b8 rest_available = 0;

lstr_t inn_text = NLSTR();
char inn_text_buf[256];

void switch_char(u8 charid) {
	cmd = CMD_SWITCH_CHAR;
	cmd_charid = charid;

	lt_printf("Selecting character %ud on page %ud\n", charid % 7, characters[charid].page);

	LT_ASSERT(charid < character_count);
}

#include <lt/time.h>
#include <stdlib.h>
#include <math.h>

void on_msg(lt_arena_t* arena, lt_socket_t* sock, lt_json_t* it) {
// 	lt_json_print(lt_stdout, it);
	if (lt_lstr_eq(it->str_val, CLSTR("play"))) {
		lt_printf("Successfully connected to '%s'\n", HOST);
	}
	else if (lt_lstr_eq(it->str_val, CLSTR("update"))) {
// 		lt_json_print(lt_stdout, it->next);

		static double delta_total = 0.0f;
		static usz delta_count = 0;

		static u64 ltime = 0;
		u64 time = lt_hfreq_time_usec();
		u64 time_msec = time/1000;
		u64 delta_usec = time - ltime;
		u64 delta_msec = delta_usec/1000;
		double delta_avg_usec = delta_total/(double)delta_count;
		u64 delta_avg_msec = round(delta_avg_usec/1000);
		if (ltime) {
			delta_total += (double)delta_usec;
			++delta_count;
// 			lt_printf("Delta %uqms, avg. %uqms\n", delta_msec, delta_avg_msec);
		}
		ltime = time;

		lt_mutex_lock(state_lock);
		send_begin();

		lstr_t local_player_slug = lt_json_find_child(it->next, CLSTR("playerSlug"))->str_val;

		lt_json_t* pieces = lt_json_find_child(it->next, CLSTR("pieces"));
		lt_json_t* piece_it = pieces->child;

		lt_json_t* logout = lt_json_find_child(pieces, CLSTR("Switch|picture/world/logout"));

		lt_json_t* botbar_inv = lt_json_find_child(pieces, CLSTR("Picture|world/bottom-bar/icons/inventory"));
		lt_json_t* botbar_spellb = lt_json_find_child(pieces, CLSTR("Picture|world/bottom-bar/icons/spellbook"));
		lt_json_t* botbar_stats = lt_json_find_child(pieces, CLSTR("Picture|world/bottom-bar/icons/stats"));

		u8 lstate = retro_state;

		if (lt_json_find_child(pieces, CLSTR("Label|character-select/title")))
			retro_state = RETRO_CHARSEL;
		else if (logout) {
			can_move = 1;

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
// 		else
// 			LT_ASSERT_NOT_REACHED();

		lt_json_t* talked_npc_name = lt_json_find_child(pieces, CLSTR("Label|world/talked-npc/dialogue/name"));
		lt_json_t* talked_npc_dialogue = lt_json_find_child(pieces, CLSTR("Label|world/talked-npc/dialogue/contents"));
		if (talked_npc_name && talked_npc_dialogue) {
			lstr_t name = lt_json_find_child(talked_npc_name, CLSTR("text"))->str_val;
			lstr_t text = lt_json_find_child(talked_npc_dialogue, CLSTR("text"))->str_val;
			if (lstate != RETRO_DIALOGUE) {
				add_chat_msg(asprintf(arena, "[%S] %S", name, text), CHAT_NPC_CLR);
				send_key("Space");
			}
			retro_state = RETRO_DIALOGUE;
			can_move = 0;
		}

		lt_json_t* opened_chest_reward = lt_json_find_child(pieces, CLSTR("Label|world/opened-chest/reward"));
		if (opened_chest_reward) {
			lstr_t text = lt_json_find_child(opened_chest_reward, CLSTR("text"))->str_val;
			if (lstate != RETRO_CHEST_REWARD) {
				add_chat_msg(asprintf(arena, "Found %S", text), CHAT_NPC_CLR);
				send_key("Space");
			}
			retro_state = RETRO_CHEST_REWARD;
			can_move = 0;
		}

		lt_json_t* bank_section = lt_json_find_child(pieces, CLSTR("Label|world/bank/section-toggle"));
		if (bank_section) {
			lstr_t switch_section_text = lt_json_find_child(bank_section, CLSTR("text"))->str_val;
			lt_printf("%S\n", switch_section_text);
			if (cmd == CMD_BANK_CLOSE) {
				send_key("Space");
				cmd = 0;
			}
			retro_state = RETRO_BANK;
			can_move = 0;

			if (retro_state != lstate)
				popup_open = 1;
		}

		lt_json_t* npc_shop_close = lt_json_find_child(pieces, CLSTR("Switch|picture/world/talked-npc/shop/close"));
		if  (npc_shop_close) {
			retro_state = RETRO_NPC_TRADE;
			can_move = 0;
		}

		lt_json_t* inn_text_js = lt_json_find_child(pieces, CLSTR("Label|world/talked-npc/inn"));
		if (inn_text_js) {
			lt_json_t* inn_yes = lt_json_find_child(pieces, CLSTR("Switch|button/world/talked-npc/inn/yes"));
			lt_json_t* inn_no = lt_json_find_child(pieces, CLSTR("Switch|button/world/talked-npc/inn/no"));

			rest_available = 0;
			if (inn_yes && inn_no) {
				rest_available = 1;

				if (cmd == CMD_INN_ACCEPT) {
					isz x = lt_json_int_val(lt_json_find_child(inn_yes, CLSTR("x")));
					isz y = lt_json_int_val(lt_json_find_child(inn_yes, CLSTR("y")));
					send_click(x + 1, y + 1);
					cmd = 0;
				}
				else if (cmd == CMD_INN_REJECT) {
					isz x = lt_json_int_val(lt_json_find_child(inn_no, CLSTR("x")));
					isz y = lt_json_int_val(lt_json_find_child(inn_no, CLSTR("y")));
					send_click(x + 1, y + 1);
					cmd = 0;
				}

			}

			lstr_t text = lt_json_find_child(inn_text_js, CLSTR("text"))->str_val;
			memcpy(inn_text_buf, text.str, text.len);
			inn_text = LSTR(inn_text_buf, text.len);

			retro_state = RETRO_INN;
			can_move = 0;

			if (retro_state != lstate)
				popup_open = 1;
		}

		interaction_str = LSTR(interaction_str_buf, 0);
		lt_json_t* interaction_js = lt_json_find_child(pieces, CLSTR("Label|button/world/interact"));
		if (interaction_js) {
			lstr_t text = lt_json_find_child(interaction_js, CLSTR("text"))->str_val;
			interaction_str.len = text.len;
			memcpy(interaction_str_buf, text.str, text.len);
		}

// 		if (lstate != retro_state) {
// 			if (retro_state == RETRO_WORLD || retro_state == RETRO_STATS)
// 				send_key("KeyC");
// 			else if (retro_state == RETRO_INVENTORY)
// 				send_key("KeyX");
// 			else if (retro_state == RETRO_SPELLBOOK)
// 				send_key("KeyZ");
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

				send_click(x + 1, y + 1);

 				sel_charid = cmd_charid;
 				cmd = 0;
 				inv_item_count = 0;
 				gold = NLSTR();
			}
			else if (cmd == CMD_GET_CHARS || (cmd == CMD_SWITCH_CHAR && charsel_pageid != characters[cmd_charid].page)) {
				if (!awaiting_pageswitch) {
					lt_printf("Sending 'next page' click on page %ud\n", charsel_pageid);
					send_click(223, 203);
					awaiting_pageswitch = 1;
				}
			}
		}
		else if (logout) {
			if (cmd == CMD_SWITCH_CHAR || cmd == CMD_GET_CHARS) {
				int x = lt_json_int_val(lt_json_find_child(logout, CLSTR("x")));
				int y = lt_json_int_val(lt_json_find_child(logout, CLSTR("y")));

				send_click(x + 1, y + 1);
			}
		}

		while (piece_it) {
			if (lt_lstr_startswith(piece_it->key, CLSTR("Player|"))) {
				usz pfx_len = CLSTR("Player|").len;

				lstr_t slug = LSTR(piece_it->key.str + pfx_len, piece_it->key.len - pfx_len);
				player_t* player = find_player_from_slug(slug);

				lstr_t username;
				if (piece_it->stype == LT_JSON_OBJECT)
					username = lt_json_find_child(piece_it, CLSTR("username"))->str_val;
				else if (piece_it->stype == LT_JSON_STRING)
					username = piece_it->str_val;

				if (!player)
					player = new_player(slug, username);

				if (piece_it->stype != LT_JSON_OBJECT)
					continue;

				if (lt_lstr_eq(slug, local_player_slug))
					local_player = player;

				lt_json_t* clothes_dye_item_slug = lt_json_find_child(piece_it, CLSTR("clothesDyeItemSlug"));
				if (clothes_dye_item_slug->stype != LT_JSON_NULL) {
					item_t* dye_item = find_item(clothes_dye_item_slug->str_val);
					player->clothes_dye = dye_item->dye;
				}
				else
					player->clothes_dye = NULL;

				lt_json_t* hair_dye_item_slug = lt_json_find_child(piece_it, CLSTR("hairDyeItemSlug"));
				if (hair_dye_item_slug->stype != LT_JSON_NULL) {
					item_t* dye_item = find_item(clothes_dye_item_slug->str_val);
					player->hair_dye = dye_item->dye;
				}
				else
					player->hair_dye = NULL;

				lt_json_t* tilemap_js = lt_json_find_child(piece_it, CLSTR("tilemapSlug"));
				if (tilemap_js->stype == LT_JSON_STRING)
					player->tilemap = find_tilemap(tilemap_js->str_val);
				else
					player->tilemap = NULL;

				lt_json_t* x_js = lt_json_find_child(piece_it, CLSTR("x"));
				lt_json_t* y_js = lt_json_find_child(piece_it, CLSTR("y"));
				if (x_js->stype != LT_JSON_NULL && y_js->stype != LT_JSON_NULL) {
					player->x = lt_json_int_val(x_js);
					player->y = lt_json_int_val(y_js);
				}

				lt_json_t* dir_js = lt_json_find_child(piece_it, CLSTR("direction"));
				if (dir_js->stype != LT_JSON_NULL)
					player->direction = find_direction(dir_js->str_val);

				lt_json_t* mask_js = lt_json_find_child(piece_it, CLSTR("maskItemSlug"));
				if (mask_js->stype != LT_JSON_NULL)
					player->mask = find_mask(mask_js->str_val);
				else
					player->mask = NULL;

				lt_json_t* outfit_js = lt_json_find_child(piece_it, CLSTR("outfitItemSlug"));
				if (outfit_js->stype != LT_JSON_NULL)
					player->outfit = find_outfit(outfit_js->str_val);
				else
					player->outfit = NULL;

				lt_json_t* wstart_js = lt_json_find_child(piece_it, CLSTR("sinceWalkAnimationStarted"));
				if (wstart_js->stype != LT_JSON_NULL && player != local_player) {
					if (time_msec - player->walk_start > MOVESPEED - delta_avg_msec)
						player->walk_start = time_msec - lt_json_int_val(wstart_js);
				}
				else
					player->walk_start = time_msec - MOVESPEED*2;
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
			else if (lt_lstr_startswith(piece_it->key, CLSTR("Label|world/inventory/bag/"))) {
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
					chests[chest_index].opened_at = lt_json_int_val(opened_at);
				else
					chests[chest_index].opened_at = -1;
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

		if (retro_state == RETRO_INVENTORY) {
			lt_json_t* gold_json = lt_json_find_child(pieces, CLSTR("Label|world/inventory/gold"));
			lstr_t gold_str = lt_json_find_child(gold_json, CLSTR("text"))->str_val;
			gold.str = gold_buf;
			gold.len = gold_str.len;
			memcpy(gold_buf, gold_str.str, gold_str.len);
		}

		lt_json_t* chats = lt_json_find_child(it->next, CLSTR("chats"));
		lt_json_t* chat_it = chats->child;
		while (chat_it) {
			lstr_t type = lt_json_find_child(chat_it, CLSTR("type"))->str_val;
			lstr_t player_slug = lt_json_find_child(chat_it, CLSTR("playerSlug"))->str_val;

			if (lt_lstr_eq(type, CLSTR("logout"))) {
				lstr_t username = find_player_from_slug(player_slug)->username;
				add_chat_msg(asprintf(arena, "%S has logged out", username), CHAT_SERVER_CLR);
			}
			else if (lt_lstr_eq(type, CLSTR("message"))) {
				lstr_t username = find_player_from_slug(player_slug)->username;
				lstr_t msg = lt_json_find_child(chat_it, CLSTR("contents"))->str_val;
// 				lstr_t channel = lt_json_find_child(chat_it, CLSTR("channel"))->str_val;
				add_chat_msg(asprintf(arena, "[%S] %S", username, msg), CHAT_PLAYER_CLR);
			}
			else if (lt_lstr_eq(type, CLSTR("login"))) {
				lstr_t username = find_player_from_slug(player_slug)->username;
				add_chat_msg(asprintf(arena, "%S has logged in", username), CHAT_SERVER_CLR);
			}
			chat_it = chat_it->next;
		}

		last_pageid = charsel_pageid;

		send_end(sock);
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
			LT_ASSERT(recv_fixed(sock, frame, 2) > 0);
			payload_len = frame[1] | (frame[0] << 8);
		}
		else if (payload_len == 127) {
			LT_ASSERT(recv_fixed(sock, frame, 8) > 0);
			payload_len = (u64)frame[7] | ((u64)frame[6] << 8) | ((u64)frame[5] << 16) | ((u64)frame[4] << 24) |
					 ((u64)frame[3] << 32) | ((u64)frame[2] << 40) | ((u64)frame[1] << 48) | ((u64)frame[0] << 56);
		}

		LT_ASSERT(payload_len < LT_MB(1));

		char* payload = lt_arena_reserve(arena, payload_len);

		LT_ASSERT(recv_fixed(sock, payload, payload_len) > 0);

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

void draw_sprite_uv(lt_vec2_t pos, lt_vec2_t size, lt_vec2_t uv1, lt_vec2_t uv2, int tex) {
	float x1 = pos[0], y1 = pos[1];
	float x2 = x1 + size[0], y2 = y1 + size[1];

	lt_vec3_t verts[6] = {
		{x1, y1, 0.1f},
		{x2, y1, 0.1f},
		{x2, y2, 0.1f},
		{x1, y1, 0.1f},
		{x2, y2, 0.1f},
		{x1, y2, 0.1f},
	};
	lt_vec4_t clrs[6] = {
		{1.0f, 1.0f, 1.0f, 1.0f},
		{1.0f, 1.0f, 1.0f, 1.0f},
		{1.0f, 1.0f, 1.0f, 1.0f},
		{1.0f, 1.0f, 1.0f, 1.0f},
		{1.0f, 1.0f, 1.0f, 1.0f},
		{1.0f, 1.0f, 1.0f, 1.0f},
	};
	lt_vec2_t uvs[6] = {
		{uv1[0], uv1[1]},
		{uv2[0], uv1[1]},
		{uv2[0], uv2[1]},
		{uv1[0], uv1[1]},
		{uv2[0], uv2[1]},
		{uv1[0], uv2[1]},
	};

	mesh_t m;
	m.verts = verts;
	m.clrs = clrs;
	m.uvs = uvs;
	m.vert_count = 6;

	render_upload_mesh(&m);
	render_mesh(&m, tex);
	render_free_mesh(&m);
}

static inline
void draw_sprite(float x, float y, float w, float h, int tex) {
	draw_sprite_uv(LT_VEC2(x, y), LT_VEC2(w, h), LT_VEC2(0.0f, 0.0f), LT_VEC2(1.0f, 1.0f), tex);
}

void draw_npc(float scr_x, float scr_y, float scr_tilew, int tex, u8 dir) {
	float uv_y = 0.25f * dir;

	draw_sprite_uv(LT_VEC2(scr_x, scr_y), LT_VEC2(scr_tilew, scr_tilew), LT_VEC2(0.0f, uv_y), LT_VEC2(0.25f, uv_y + 0.25f), tex);
}

void draw_cosmetic(float scr_x, float scr_y, float scr_tilew, int tex, u8 dir, usz frame) {
	float uv_x = 0.25f * frame;
	float uv_y = 0.25f * dir;

	draw_sprite_uv(LT_VEC2(scr_x, scr_y), LT_VEC2(scr_tilew, scr_tilew), LT_VEC2(uv_x, uv_y), LT_VEC2(uv_x + 0.25f, uv_y + 0.25f), tex);
}

void draw_tile(tilemap_t* tilemap, float scr_x, float scr_y, float scr_tilew, u16 tile) {
	tileset_t* tileset = tilemap_lookup_index(tilemap, &tile);
	LT_ASSERT(tileset);

	float ts_tilew = 1.0f / tileset->width;
	float ts_tileh = 1.0f / tileset->height;
	float ts_x = (tile % tileset->width) * ts_tilew;
	float ts_y = (tile / tileset->width) * ts_tileh;

	usz tile_count = tileset->frame_counts[tile];
	if (tile_count) {
		usz duration_total = 0;
		for (usz i = 0; i < tile_count; ++i)
			duration_total += tileset->frames[tile][i * 2];

		u64 time = lt_hfreq_time_msec() % duration_total;

		duration_total = 0;
		for (usz i = 0; i < tile_count; ++i) {
			u16 frame_tile = tileset->frames[tile][i * 2 + 1];
			u16 duration = tileset->frames[tile][i * 2];

			if (time < duration_total + duration) {
				ts_x = (frame_tile % tileset->width) * ts_tilew;
				ts_y = (frame_tile / tileset->width) * ts_tileh;
				break;
			}

			duration_total += duration;
		}
	}

	draw_sprite_uv(LT_VEC2(scr_x, scr_y), LT_VEC2(scr_tilew, scr_tilew), LT_VEC2(ts_x, ts_y), LT_VEC2(ts_x + ts_tilew, ts_y + ts_tilew), tileset->texture);
}

b8 collide_at(tilemap_t* tilemap, isz x, isz y) {
	if (x < 0 || y < 0 || x >= tilemap->w || y >= tilemap->h)
		return 1;

	return tilemap->collision[y * tilemap->w + x];
}

void parse_data_defs(void) {
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
		else if (lt_lstr_startswith(ddef_it->key, CLSTR("ClothesColor|")))
			clothes_color_add(arena, ddef_it);
		else if (lt_lstr_startswith(ddef_it->key, CLSTR("HairColor|")))
			hair_color_add(arena, ddef_it);
		ddef_it = ddef_it->next;
	}

	ddef_it = ddefs_js->child;
	while (ddef_it) {
		if (lt_lstr_startswith(ddef_it->key, CLSTR("Tileset|")))
			tileset_add(arena, ddef_it);
		else if (lt_lstr_startswith(ddef_it->key, CLSTR("ClothesDye|")))
			clothes_dye_add(arena, ddef_it);
		else if (lt_lstr_startswith(ddef_it->key, CLSTR("HairDye|")))
			hair_dye_add(arena, ddef_it);
		ddef_it = ddef_it->next;
	}

	ddef_it = ddefs_js->child;
	while (ddef_it) {
		if (lt_lstr_startswith(ddef_it->key, CLSTR("Tilemap|")))
			tilemap_add(arena, ddef_it);
		else if (lt_lstr_startswith(ddef_it->key, CLSTR("Item|")))
			item_add(arena, ddef_it);
		ddef_it = ddef_it->next;
	}
}

lstr_t authenticate(lstr_t user, lstr_t pass) {
	lstr_t login_json = asprintf(arena, "{\"email\":\"%S\",\"password\":\"%S\",\"hcaptchaToken\":\"\"}", user, pass);

	lstr_t request = asprintf(arena,
		"POST /authenticate HTTP/1.1\r\n"
		"Host: %s\r\n"
		"Content-Length: %uz\r\n"
		"Content-Type: application/json\r\n"
		"Accept: application/json\r\n"
		"Connection: keep-alive\r\n"
		"\r\n"
		"%S",
		HOST, login_json.len, login_json
	);
	lt_socket_send(sock, request.str, request.len);

	lstr_t token = LSTR(lt_arena_reserve(arena, 128), 0);
	handle_http_response(arena, sock, token.str, &token.len);

	lt_printf("Got login token: %S\n", token);
	return token;
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

	if (!lt_gl_initialize_loader())
		lt_ferrf("Failed to load OpenGL\n");

	lstr_t token = authenticate(CLSTR(USER), CLSTR(PASS));
	parse_data_defs();

	// Upgrade to websocket
	lstr_t request = asprintf(arena,
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
	lt_socket_send(sock, request.str, request.len);
	handle_http_response(arena, sock, NULL, NULL);

	lstr_t token_json = asprintf(arena, "40{\"token\":\"%S\"}", token);
	ws_send_text(sock, token_json);

	lstr_t font_file;
	if (!lt_file_read_entire(arena, "lat1-16.psf", &font_file))
		lt_ferrf("Failed to open font file: %s\n");

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

	lt_gui_style_t style = lt_gui_default_style;

	lt_gui_ctx_t gcx, *cx = &gcx;
	gcx.cont_max = 128;
	gcx.style = &style;
	gcx.draw_rect = render_draw_rect;
	gcx.draw_text = render_draw_text;
	gcx.draw_icon = render_draw_icon;
	gcx.scissor = render_scissor;
	gcx.glyph_height = font->height;
	gcx.glyph_width = font->width;

	lt_gui_ctx_t gcx2 = gcx;

	lt_gui_ctx_init(arena, &gcx2);

	lt_gui_ctx_init(arena, cx);

	lt_thread_t* recv_thread = lt_thread_create(arena, recv_thread_proc, NULL);

	#define SIDEBAR_W 225

	b8 playerlist_state = 0;
	u32 charlist_state = 0;

	render_init();

	lt_arena_t* arena = lt_arena_alloc(LT_MB(1));
	render_arena = arena;

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
		send_begin();
		tilemap_t* tilemap = local_player ? local_player->tilemap : NULL;

		u64 time_msec = lt_hfreq_time_msec();

		static isz predict_x = 0;
		static isz predict_y = 0;
		static i8 predict_move_dir = -1;
		float predict_step_len = 0.0f;

		static i64 w_pressed_msec = -1;
		static i64 a_pressed_msec = -1;
		static i64 s_pressed_msec = -1;
		static i64 d_pressed_msec = -1;

		static u64 walk_start_msec = 0;
		static u8 step_anim_offs = 0;

		for (usz i = 0; i < ev_count; ++i) {
			lt_window_event_t ev = evs[i];
			if (ev.type == LT_WIN_EVENT_KEY_RELEASE) {
				if (ev.key == LT_KEY_W)
					w_pressed_msec = -1;
				else if (ev.key == LT_KEY_A)
					a_pressed_msec = -1;
				else if (ev.key == LT_KEY_S)
					s_pressed_msec = -1;
				else if (ev.key == LT_KEY_D)
					d_pressed_msec = -1;
			}
			else if (ev.type == LT_WIN_EVENT_KEY_PRESS) {
				if (ev.key == LT_KEY_W)
					w_pressed_msec = time_msec;
				else if (ev.key == LT_KEY_A)
					a_pressed_msec = time_msec;
				else if (ev.key == LT_KEY_S)
					s_pressed_msec = time_msec;
				else if (ev.key == LT_KEY_D)
					d_pressed_msec = time_msec;
				else
					continue;

				if (popup_open && !textbox_selected) {
					popup_open = 0;
					send_key("Space");
				}
			}
		}

		if (tilemap && local_player && can_move && !textbox_selected) {
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

			u64 walk_time_delta = time_msec - walk_start_msec;

			static u64 mkeyup_delay = 0;
#define WALK_KEYUP_DELAY (MOVESPEED/2)

			if (mkeyup_delay && walk_time_delta > mkeyup_delay) {
				send_key_up("KeyW");
				send_key_up("KeyA");
				send_key_up("KeyS");
				send_key_up("KeyD");
				mkeyup_delay = 0;
			}

			if (walk_time_delta > MOVESPEED) {
				usz prediction_diff = abs(predict_x - local_player->x) + abs(predict_y - local_player->y);

				if (prediction_diff > 1 || (move_dir == -1 && walk_time_delta > MOVESPEED*2)) {
					predict_move_dir = local_player->direction;
					predict_x = local_player->x;
					predict_y = local_player->y;
				}

				if (move_dir != -1) {
					predict_move_dir = move_dir;

					switch (move_dir) {
					case DIR_UP:
						send_key_down("KeyW");
						mkeyup_delay = 50;
						if (!collide_at(tilemap, predict_x, predict_y - 1)) {
							--predict_y;
							goto nocollide_common;
						}
						break;
					case DIR_LEFT:
						send_key_down("KeyA");
						mkeyup_delay = 50;
						if (!collide_at(tilemap, predict_x - 1, predict_y)) {
							--predict_x;
							goto nocollide_common;
						}
						break;
					case DIR_DOWN:
						send_key_down("KeyS");
						mkeyup_delay = 50;
						if (!collide_at(tilemap, predict_x, predict_y + 1)) {
							++predict_y;
							goto nocollide_common;
						}
						break;
					case DIR_RIGHT:
						send_key_down("KeyD");
						mkeyup_delay = 50;
						if (!collide_at(tilemap, predict_x + 1, predict_y)) {
							++predict_x;
							goto nocollide_common;
						}
						break;

					nocollide_common:
						walk_start_msec = time_msec;
						mkeyup_delay = WALK_KEYUP_DELAY;
						step_anim_offs = !step_anim_offs << 1;
						break;
					}
				}
			}

			predict_step_len = 1.0f - ((float)(time_msec - walk_start_msec) / MOVESPEED);
		}

		lt_gui_begin(cx, 0, 0, width, height);

		lt_gui_panel_begin(cx, 0, 24, 0);

		lt_gui_row(cx, 6);
		if (lt_gui_dropdown_begin(cx, CLSTR("Character"), 97, character_count * 18, &charlist_state, 0)) {
			for (usz i = 0; i < character_count; ++i) {
				if (lt_gui_button(cx, characters[i].description, LT_GUI_ALIGN_RIGHT | LT_GUI_BORDER_OUTSET | LT_GUI_GROW_X))
					switch_char(i);
			}
			lt_gui_dropdown_end(cx);
		}

		if (lt_gui_button(cx, CLSTR("Create"), 0))
			cmd = CMD_CREATE_CHAR;

		lt_gui_hspace(cx, 4, 0);
		lt_gui_label(cx, local_player ? local_player->username : NLSTR(), 0);

		if (lt_gui_button(cx, CLSTR("Log out"), LT_GUI_ALIGN_RIGHT | LT_GUI_BORDER_OUTSET))
			quit = 1;
		if (lt_gui_button(cx, CLSTR("Settings"), LT_GUI_ALIGN_RIGHT | LT_GUI_BORDER_OUTSET))
			;

		lt_gui_panel_end(cx);

		lt_gui_row(cx, 2);
		u32 panel_bg = cx->style->panel_bg_clr;
		cx->style->panel_bg_clr = 0x00FFFFFF;
		lt_gui_panel_begin(cx, -SIDEBAR_W, 0, LT_GUI_BORDER_INSET);
		lt_gui_rect_t game_area = lt_gui_get_container(cx)->a;
		lt_gui_panel_end(cx);
		cx->style->panel_bg_clr = panel_bg;

		lt_gui_panel_begin(cx, 0, 0, LT_GUI_BORDER_INSET);
		if (lt_gui_expandable(cx, CLSTR("Players"), &playerlist_state, LT_GUI_BORDER_OUTSET)) {
			lt_gui_panel_begin(cx, 0, (font->height) * player_count + 6 + (2 * player_count - 1), LT_GUI_BORDER_OUTSET);
			for (usz i = 0; i < player_count; ++i)
				lt_gui_label(cx, players[i].username, 0);
			lt_gui_panel_end(cx);
		}

		lt_gui_panel_begin(cx, 0, -20, LT_GUI_BORDER_OUTSET);
		u32 text_clr = cx->style->text_clr;
		for (usz i = 0; i < chat_msg_count; ++i) {
			cx->style->text_clr = chat_msgs[i].clr;
			lt_gui_text(cx, chat_msgs[i].text, 0);
		}
		cx->style->text_clr = text_clr;
		lt_gui_panel_end(cx);

		if ((textbox_selected = lt_gui_textbox(cx, 0, 0, &tb_state, LT_GUI_BORDER_INSET))) {
			for (usz i = 0; i < ev_count; ++i) {
				lt_window_event_t ev = evs[i];
				if (ev.type == LT_WIN_EVENT_KEY_PRESS) {
					usz len = strnlen(tb_buf, CHATMSG_MAXLEN - 1);
					if (ev.key >= LT_KEY_PRINTABLE_MIN && ev.key <= LT_KEY_PRINTABLE_MAX && len < CHATMSG_MAXLEN) {
						tb_buf[len] = ev.key;
						if (len < (CHATMSG_MAXLEN - 1))
							tb_buf[len + 1] = 0;
					}
					else if (ev.key == LT_KEY_BACKSPACE && len)
						tb_buf[len - 1] = 0;
					else if (ev.key == LT_KEY_ENTER && len) {
						send_chat(CLSTR("global"), LSTR(tb_buf, len));
						tb_buf[0] = 0;
					}
				}
			}
		}

		if (lt_window_key_pressed(win, LT_KEY_ENTER))
			tb_state.selected = !tb_state.selected;

		lt_gui_panel_end(cx);

		lt_gui_end(cx);

		render_scissor(NULL, &game_area);

#define scr_tilew 32.0f

		if (tilemap && cmd != CMD_GET_CHARS && cmd != CMD_SWITCH_CHAR) {
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

// 			for (usz x = 0; x < tilemap->w; ++x) {
// 				for (usz y = 0; y < tilemap->h; ++y) {
// 					usz ii = x * tilemap->h + y;
// 					u16 tile_index = tilemap->b_tile_indices[ii * 2];
// 					u16 tile_count = tilemap->b_tile_indices[ii * 2 + 1];

// 					float scr_x = scr_tileoffs_x + x * scr_tilew;
// 					float scr_y = scr_tileoffs_y + y * scr_tilew;

// 					for (usz ti = 0; ti < tile_count; ++ti) {
// 						u16 tile = tilemap->tiles[tile_index + ti];
// 						draw_tile(tilemap, scr_x, scr_y, scr_tilew, tile);
// 					}

// 					i16 chest_index = tilemap->chest_indices[ii];
// 					if (chest_index >= 0) {
// 						tileset_t* ts = tilemap_lookup_index(tilemap, &chest_index);
// 						chest_t* chest = &chests[ts->chests[chest_index]];

// 						float uv_x = 0.0f;
// 						if (chest->opened_at != -1)
// 							uv_x += 0.75f;
// 						draw_sprite_uv(LT_VEC2(scr_x, scr_y), LT_VEC2(scr_tilew, scr_tilew), LT_VEC2(uv_x, 0.0f), LT_VEC2(uv_x + 0.25f, 1.0f), chest->texture);
// 					}

// 					i16 bank_index = tilemap->bank_indices[ii];
// 					if (bank_index >= 0) {
// 						tileset_t* ts = tilemap_lookup_index(tilemap, &bank_index);
// 						bank_t* bank = &banks[ts->banks[bank_index]];

// 						draw_sprite_uv(LT_VEC2(scr_x, scr_y), LT_VEC2(scr_tilew, scr_tilew), LT_VEC2(0.0f, 0.0f), LT_VEC2(0.25f, 1.0f), bank->texture);
// 					}

// 					i16 npc_index = tilemap->npc_indices[ii];
// 					if (npc_index >= 0) {
// 						tileset_t* ts = tilemap_lookup_index(tilemap, &npc_index);
// 						npc_t* npc = &npcs[ts->npcs[npc_index]];

// 						draw_npc(scr_x, scr_y, scr_tilew, npc->texture, npc->direction);
// 						draw_sprite(scr_x, scr_y - scr_tilew, scr_tilew, scr_tilew, npc->indicator_texture);
// 					}
// 				}
// 			}


 			render_model_offs(scr_tileoffs_x, scr_tileoffs_y);
			for (usz i = 0; i < tilemap->tileset_count; ++i)
				render_mesh(&tilemap->meshes[i], tilemap->tilesets[i]->texture);
			render_model_offs(0, 0);

			lt_gui_point_t* pname_pts = lt_arena_reserve(arena, player_count * sizeof(lt_gui_point_t));
			lstr_t* pname_strs = lt_arena_reserve(arena, player_count * sizeof(lstr_t));
			u32* pname_clrs = lt_arena_reserve(arena, player_count * sizeof(u32));
			usz pname_count = 0;

			lt_gui_rect_t* pname_bg_rects = lt_arena_reserve(arena, player_count * sizeof(lt_gui_rect_t));
			u32* pname_bg_clrs = lt_arena_reserve(arena, player_count * sizeof(u32));

			for (usz i = 0; i < player_count; ++i) {
				if (players[i].tilemap != tilemap)
					continue;

				u64 walk_anim_start = players[i].walk_start;
				float step_len = 1.0f - ((float)(time_msec - walk_anim_start) / MOVESPEED);

				isz x = players[i].x;
				isz y = players[i].y;
				u8 dir = players[i].direction;
				u8 step_offs = 0;

				b8 is_local = &players[i] == local_player;

				if (is_local) {
					dir = predict_move_dir;
					x = predict_x;
					y = predict_y;
					walk_anim_start = walk_start_msec;
					step_offs = step_anim_offs;

					step_len = predict_step_len;
				}

				float scr_x = scr_tileoffs_x + x * scr_tilew;
				float scr_y = scr_tileoffs_y + y * scr_tilew;

				usz animation_frame = step_offs;

				if (step_len > 0.0f) {
					switch (dir) {
					case DIR_UP: scr_y += step_len * scr_tilew; break;
					case DIR_DOWN: scr_y -= step_len * scr_tilew; break;
					case DIR_LEFT: scr_x += step_len * scr_tilew; break;
					case DIR_RIGHT: scr_x -= step_len * scr_tilew; break;
					}

					if (step_len < 1.0f)
						animation_frame += step_len * 2;
				}

				outfit_t* outfit = players[i].outfit;
				if (outfit)
					draw_cosmetic(scr_x, scr_y, scr_tilew, outfit->texture_m, dir, animation_frame);
				mask_t* mask = players[i].mask;
				if (mask)
					draw_cosmetic(scr_x, scr_y, scr_tilew, mask->texture_m, dir, animation_frame);

				lstr_t text = players[i].username;

				if (is_local && interaction_str.len) {
					text = asprintf(arena, "(E) %S", interaction_str);
					if (lt_window_key_pressed(win, LT_KEY_E))
						send_key("Space");
				}

				float name_w = font->width * text.len;
				float name_h = font->height;

				float name_x = scr_x + scr_tilew/2 - name_w/2;
				float name_y = scr_y - name_h;

				pname_pts[pname_count] = LT_GUI_POINT(round(name_x), round(name_y));
				pname_strs[pname_count] = text;
				pname_clrs[pname_count] = 0xFFFFFFFF;

				pname_bg_rects[pname_count] = LT_GUI_RECT(round(name_x - 2.0f), round(name_y - 1.0f), round(name_w + 4.0f), round(name_h + 2.0f));
				pname_bg_clrs[pname_count] = 0xFF000000;
				if (!is_local || interaction_str.len)
					++pname_count;
			}

// 			for (usz y = 0; y < tilemap->h; ++y) {
// 				for (usz x = 0; x < tilemap->w; ++x) {
// 					usz ii = x * tilemap->h + y;
// 					u16 tile_index = tilemap->a_tile_indices[ii * 2];
// 					u16 tile_count = tilemap->a_tile_indices[ii * 2 + 1];

// 					float scr_x = scr_tileoffs_x + x * scr_tilew;
// 					float scr_y = scr_tileoffs_y + y * scr_tilew;

// 					for (usz ti = 0; ti < tile_count; ++ti) {
// 						u16 tile = tilemap->tiles[tile_index + ti];
// 						draw_tile(tilemap, scr_x, scr_y, scr_tilew, tile);
// 					}
// 				}
// 			}

			for (usz i = 0; i < pname_count; ++i) {
				render_draw_rect(NULL, 1, &pname_bg_rects[i], &pname_bg_clrs[i]);
				render_draw_text(NULL, 1, &pname_pts[i], &pname_strs[i], &pname_clrs[i]);
			}
		}

		render_scissor(NULL, NULL);

		cx = &gcx2;

		lt_window_mouse_pos(win, &cx->mouse_x, &cx->mouse_y);
		cx->mouse_state = lt_window_key_pressed(win, LT_KEY_MB1);

#define POPUP_W 400
#define POPUP_H 105

		usz center_x = game_area.x + game_area.w/2 - POPUP_W/2;
		usz center_y = game_area.y + game_area.h/2 - POPUP_H - scr_tilew;

		lt_gui_begin(cx, center_x, center_y, 400, 106);
		if (retro_state == RETRO_INN && popup_open) {
			lt_gui_panel_begin(cx, 0, 0, LT_GUI_BORDER_OUTSET);

			lt_gui_panel_begin(cx, 0, 24, LT_GUI_BORDER_INSET);
			lt_gui_row(cx, 2);
			lt_gui_label(cx, CLSTR("Inn"), 0);
			if (lt_gui_button(cx, CLSTR("(Q) Close"), LT_GUI_ALIGN_RIGHT) || lt_window_key_pressed(win, LT_KEY_Q)) {
				cmd = CMD_INN_REJECT;
				popup_open = 0;
			}
			lt_gui_panel_end(cx);

			lt_gui_panel_begin(cx, 0, 0, LT_GUI_BORDER_INSET);
			lt_gui_text(cx, inn_text, 0);
			if (rest_available && (lt_gui_button(cx, CLSTR("(E) Rest"), 0) || lt_window_key_pressed(win, LT_KEY_E))) {
				cmd = CMD_INN_ACCEPT;
				popup_open = 0;
			}
			lt_gui_panel_end(cx);

			lt_gui_panel_end(cx);
		}
		if (retro_state == RETRO_BANK && popup_open) {
			lt_gui_panel_begin(cx, 0, 0, LT_GUI_BORDER_OUTSET);

			lt_gui_panel_begin(cx, 0, 24, LT_GUI_BORDER_INSET);
			lt_gui_row(cx, 2);
			lt_gui_label(cx, CLSTR("Bank"), 0);
			if (lt_gui_button(cx, CLSTR("(Q) Close"), LT_GUI_ALIGN_RIGHT) || lt_window_key_pressed(win, LT_KEY_Q)) {
				cmd = CMD_BANK_CLOSE;
				popup_open = 0;
			}
			lt_gui_panel_end(cx);

			usz w = POPUP_W/2 - cx->style->spacing;

			lt_gui_row(cx, 2);
			lt_gui_panel_begin(cx, w, 0, LT_GUI_BORDER_INSET);
			lt_gui_panel_end(cx);
			lt_gui_panel_begin(cx, 0, 0, LT_GUI_BORDER_INSET);
			lt_gui_panel_end(cx);

			lt_gui_panel_end(cx);
		}
		lt_gui_end(cx);

		cx = &gcx;

		send_end(sock);
		lt_mutex_release(state_lock);

		render_end(win);

		lt_arena_restore(arena, &arestore);
	}

	lt_thread_join(recv_thread);

	lt_socket_destroy(sock);

	lt_window_destroy(win);

	lt_window_terminate();
}

