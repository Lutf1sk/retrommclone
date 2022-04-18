#include "npc.h"
#include "game_helpers.h"

#include <lt/json.h>

npc_t* npcs = NULL;
usz npc_count = 0;

i8 find_npc_index(lstr_t slug) {
	for (usz i = 0; i < npc_count; ++i)
		if (lt_lstr_eq(slug, npcs[i].slug))
			return i;
	return -1;
}

npc_t* npc_add(lt_arena_t* arena, lt_json_t* json) {
	npcs = realloc(npcs, (npc_count + 1) * sizeof(npc_t));

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

