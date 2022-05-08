#ifndef MAP_H
#define MAP_H 1

#include <lt/lt.h>
#include <lt/fwd.h>

typedef struct mesh mesh_t;

typedef
struct tileset {
	lstr_t slug;
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
	lstr_t slug;
	u16 w, h;
	tileset_t** tilesets;
	mesh_t* meshes;
	b8* collision;
	usz tileset_count;
} tilemap_t;

#define MAX_TILEMAPS 16

#define SCR_TILEW 32.0f

extern tilemap_t tilemaps[MAX_TILEMAPS];
extern char tilemap_names[MAX_TILEMAPS][32];
extern usz tilemap_count;

tilemap_t* find_tilemap(lstr_t slug);
tileset_t* tilemap_lookup_index(tilemap_t* tilemap, u16* tile_ptr);
tilemap_t* tilemap_add(lt_arena_t* arena, lt_json_t* json);

#endif
