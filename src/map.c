#include "map.h"
#include "game_helpers.h"

#include "chest.h"
#include "npc.h"
#include "bank.h"

#include <lt/json.h>

tileset_t tilesets[MAX_TILESETS];
char tileset_names[MAX_TILESETS][32];
usz tileset_count = 0;

tilemap_t tilemaps[MAX_TILEMAPS];
char tilemap_names[MAX_TILEMAPS][32];
usz tilemap_count = 0;

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

	u8* frame_counts = lt_arena_reserve(arena, tile_count * sizeof(u8));
	u16** frames = lt_arena_reserve(arena, tile_count * sizeof(u16*));

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

			lt_json_t* anim_frames = lt_json_find_child(tile_it, CLSTR("animationFrames"));
			if (anim_frames->child_count) {
				frame_counts[index] = anim_frames->child_count;
				frames[index] = lt_arena_reserve(arena, anim_frames->child_count * sizeof(u16) * 2);
				usz i = 0;
				for (lt_json_t* it = anim_frames->child; it; it = it->next) {
					frames[index][i++] = lt_json_uint_val(lt_json_find_child(it, CLSTR("duration")));
					frames[index][i++] = lt_json_uint_val(lt_json_find_child(it, CLSTR("index")));
				}
			}

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
	tilesets[tileset_count].frame_counts = frame_counts;
	tilesets[tileset_count].frames = frames;
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

	usz w = tiles_js->child_count;
	usz h = tiles_js->child->child_count;
	LT_ASSERT(w && h);

	usz tile_count = w * h;

	u16* a_tiles = lt_arena_reserve(arena, tile_count * 2 * sizeof(u16));
	u16* b_tiles = lt_arena_reserve(arena, tile_count * 2 * sizeof(u16));
	i16* chests = lt_arena_reserve(arena, tile_count * sizeof(i16));
	i16* banks = lt_arena_reserve(arena, tile_count * sizeof(i16));
	i16* npcs = lt_arena_reserve(arena, tile_count * sizeof(i16));
	u16* tiles = lt_arena_reserve(arena, 0);

	u16 tile_it = 0, tile_index = 0;

	for (lt_json_t* i = tiles_js->child; i; i = i->next) {
		LT_ASSERT(i->child_count == h);
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

	LT_ASSERT(tile_it/2 == tile_count);

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

