#ifndef NPC_H
#define NPC_H 1

#include <lt/lt.h>
#include <lt/fwd.h>

typedef
struct npc {
	lstr_t slug;
	lstr_t name;
	int texture;
	int indicator_texture;
	u8 direction;
} npc_t;

extern npc_t* npcs;
extern usz npc_count;

i8 find_npc_index(lstr_t slug);

npc_t* npc_add(lt_arena_t* arena, lt_json_t* json);

#endif
