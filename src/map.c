#include "map.h"
#include "game_helpers.h"

#include "chest.h"
#include "npc.h"
#include "bank.h"
#include "render.h"

#include <lt/json.h>

#define BDEPTH 0.1f
#define ADEPTH 0.05f

tileset_t tilesets[MAX_TILESETS];
char tileset_names[MAX_TILESETS][32];
usz tileset_count = 0;

tilemap_t tilemaps[MAX_TILEMAPS];
char tilemap_names[MAX_TILEMAPS][32];
usz tilemap_count = 0;

tilemap_t* find_tilemap(lstr_t slug) {
	for (usz i = 0; i < tilemap_count; ++i)
		if (lt_lstr_eq(slug, tilemaps[i].slug))
			return &tilemaps[i];
	return NULL;
}

tileset_t* find_tileset(lstr_t slug) {
	for (usz i = 0; i < tileset_count; ++i)
		if (lt_lstr_eq(slug, tilesets[i].slug))
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
	tilesets[tileset_count].slug = name;
	return &tilesets[tileset_count++];
}

tileset_t* tilemap_lookup_index(tilemap_t* tilemap, u16* tile_ptr) {
// 	u16 tile = *tile_ptr;

// 	for (usz i = 0; i < tilemap->tileset_count; ++i) {
// 		usz start = tilemap->tileset_start_indices[i];
// 		if (tile >= start && tile < start + tilemap->tilesets[i]->tile_count) {
// 			*tile_ptr -= start;
// 			return tilemap->tilesets[i];
// 		}
// 	}
	return NULL;
}

static
void add_quad(mesh_t* m, lt_vec4_t clr, float depth, float x1, float y1, float x2, float y2, float uvx1, float uvy1, float uvx2, float uvy2) {
	lt_vec3_copy(m->verts[m->vert_count], LT_VEC3(x1, y1, depth));
	lt_vec2_copy(m->uvs[m->vert_count], LT_VEC2(uvx1, uvy1));
	lt_vec4_copy(m->clrs[m->vert_count++], clr);

	lt_vec3_copy(m->verts[m->vert_count], LT_VEC3(x2, y1, depth));
	lt_vec2_copy(m->uvs[m->vert_count], LT_VEC2(uvx2, uvy1));
	lt_vec4_copy(m->clrs[m->vert_count++], clr);

	lt_vec3_copy(m->verts[m->vert_count], LT_VEC3(x2, y2, depth));
	lt_vec2_copy(m->uvs[m->vert_count], LT_VEC2(uvx2, uvy2));
	lt_vec4_copy(m->clrs[m->vert_count++], clr);

	lt_vec3_copy(m->verts[m->vert_count], LT_VEC3(x1, y1, depth));
	lt_vec2_copy(m->uvs[m->vert_count], LT_VEC2(uvx1, uvy1));
	lt_vec4_copy(m->clrs[m->vert_count++], clr);

	lt_vec3_copy(m->verts[m->vert_count], LT_VEC3(x2, y2, depth));
	lt_vec2_copy(m->uvs[m->vert_count], LT_VEC2(uvx2, uvy2));
	lt_vec4_copy(m->clrs[m->vert_count++], clr);

	lt_vec3_copy(m->verts[m->vert_count], LT_VEC3(x1, y2, depth));
	lt_vec2_copy(m->uvs[m->vert_count], LT_VEC2(uvx1, uvy2));
	lt_vec4_copy(m->clrs[m->vert_count++], clr);
}

tilemap_t* tilemap_add(lt_arena_t* arena, lt_json_t* json) {
	lt_json_t* tiles_js = lt_json_find_child(json, CLSTR("tiles"));

	usz w = tiles_js->child_count;
	usz h = tiles_js->child->child_count;
	LT_ASSERT(w && h);

	usz tile_count = w * h;

	lt_json_t* sets_js = lt_json_find_child(json, CLSTR("tilesets"));
	usz set_count = sets_js->child_count;
	tileset_t** sets = lt_arena_reserve(arena, set_count * sizeof(tileset_t*));
	mesh_t* meshes = lt_arena_reserve(arena, set_count * sizeof(mesh_t));
	u16* set_start_indices = lt_arena_reserve(arena, set_count * sizeof(u16));

	b8* collision = lt_arena_reserve(arena, tile_count * sizeof(b8));
	memset(collision, 0, tile_count * sizeof(b8));

	usz set_i = 0;
	for (lt_json_t* set_it = sets_js->child; set_it; set_it = set_it->next) {
		tileset_t* set = find_tileset(lt_json_find_child(set_it, CLSTR("tileset"))->str_val);
		u16 set_start = lt_json_uint_val(lt_json_find_child(set_it, CLSTR("firstTileID")));
		u16 set_end = set_start + set->tile_count;

		usz msize = 1024;
		mesh_t m = {malloc(msize * sizeof(lt_vec3_t)), malloc(msize * sizeof(lt_vec2_t)), malloc(msize * sizeof(lt_vec4_t)), 0};

		float uvw = 1.0f/(float)set->width;
		float uvh = 1.0f/(float)set->height;

		usz x = 0;
		for (lt_json_t* i = tiles_js->child; i; i = i->next) {
			float x1 = x * SCR_TILEW, x2 = x1 + SCR_TILEW;
			LT_ASSERT(i->child_count == h);

			lt_vec4_t clr = LT_VEC4_INIT(1.0f, 1.0f, 1.0f, 1.0f);

			usz y = 0;
			for (lt_json_t* j = i->child; j; j = j->next) {
				float y1 = y * SCR_TILEW, y2 = y1 + SCR_TILEW;

				for (lt_json_t* a_it = lt_json_find_child(j, CLSTR("aboveIndices"))->child; a_it; a_it = a_it->next) {
					u16 tile = lt_json_uint_val(a_it);
					if (tile < set_start || tile >= set_end)
						continue;
					tile -= set_start;

					float uvx1 = (tile % set->width) * uvw, uvx2 = uvx1 + uvw;
					float uvy1 = (tile / set->width) * uvh, uvy2 = uvy1 + uvh;

					usz new_vert_count = m.vert_count + 6;
					if (new_vert_count > msize) {
						msize *= 2;
						m.verts = realloc(m.verts, msize * sizeof(lt_vec3_t));
						m.uvs = realloc(m.uvs, msize * sizeof(lt_vec2_t));
						m.clrs = realloc(m.clrs, msize * sizeof(lt_vec4_t));
					}

					add_quad(&m, clr, ADEPTH, x1, y1, x2, y2, uvx1, uvy1, uvx2, uvy2);
				}

				for (lt_json_t* a_it = lt_json_find_child(j, CLSTR("belowIndices"))->child; a_it; a_it = a_it->next) {
					u16 tile = lt_json_uint_val(a_it);
					if (tile < set_start || tile >= set_end)
						continue;
					tile -= set_start;

					// TODO: the collision should be calculated in a separate pass
					// to prevent the "later" tilesets from overwriting the "earlier".
					collision[y * w + x] = set->collisions[tile];

					float uvx1 = (tile % set->width) * uvw, uvx2 = uvx1 + uvw;
					float uvy1 = (tile / set->width) * uvh, uvy2 = uvy1 + uvh;

					usz new_vert_count = m.vert_count + 6;
					if (new_vert_count > msize) {
						msize *= 2;
						m.verts = realloc(m.verts, msize * sizeof(lt_vec3_t));
						m.uvs = realloc(m.uvs, msize * sizeof(lt_vec2_t));
						m.clrs = realloc(m.clrs, msize * sizeof(lt_vec4_t));
					}

					add_quad(&m, clr, BDEPTH, x1, y1, x2, y2, uvx1, uvy1, uvx2, uvy2);
				}

				lt_json_t* chest = lt_json_find_child(j, CLSTR("chestIndex"));
				lt_json_t* bank = lt_json_find_child(j, CLSTR("bankIndex"));
				lt_json_t* npc = lt_json_find_child(j, CLSTR("npcIndex"));

				if (chest->stype != LT_JSON_NULL)
					;//chests[tile_it / 2] = lt_json_uint_val(chest);
				if (bank->stype != LT_JSON_NULL)
					;//banks[tile_it / 2] = lt_json_uint_val(bank);
				if (npc->stype != LT_JSON_NULL)
					;//npcs[tile_it / 2] = lt_json_uint_val(npc);

				++y;
			}

			++x;
		}

		render_upload_mesh(&m);

		meshes[set_i] = m;
		sets[set_i] = set;
		set_start_indices[set_i++] = set_start;
	}

	tilemaps[tilemap_count].w = w;
	tilemaps[tilemap_count].h = h;
	tilemaps[tilemap_count].collision = collision;
	tilemaps[tilemap_count].meshes = meshes;
	tilemaps[tilemap_count].tilesets = sets;
	tilemaps[tilemap_count].tileset_count = set_count;

	usz pfx_len = CLSTR("Tilemap|").len;
	tilemaps[tilemap_count].slug = LSTR(json->key.str + pfx_len, json->key.len - pfx_len);

	return &tilemaps[tilemap_count++];
}

