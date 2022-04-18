#ifndef MAP_H
#define MAP_H 1

#include <lt/lt.h>
#include <lt/fwd.h>

typedef
struct tileset {
	lstr_t name;
	b8* collisions;
	i8* chests, *banks, *npcs;
	u8* frame_counts;
	u16** frames;

	usz tile_count;
	usz width;
	usz height;

	int texture;
} tileset_t;

#define MAX_TILESETS 32

extern tileset_t tilesets[MAX_TILESETS];
extern char tileset_names[MAX_TILESETS][32];
extern usz tileset_count;

tileset_t* find_tileset(lstr_t slug);
tileset_t* tileset_add(lt_arena_t* arena, lt_json_t* json);

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

extern tilemap_t tilemaps[MAX_TILEMAPS];
extern char tilemap_names[MAX_TILEMAPS][32];
extern usz tilemap_count;

tilemap_t* find_tilemap(lstr_t slug);
tileset_t* tilemap_lookup_index(tilemap_t* tilemap, u16* tile_ptr);
tilemap_t* tilemap_add(lt_arena_t* arena, lt_json_t* json);

#endif
