#include "chest.h"
#include "game_helpers.h"

#include <lt/json.h>

chest_t* chests = NULL;
usz chest_count = 0;

i8 find_chest_index(lstr_t slug) {
	for (usz i = 0; i < chest_count; ++i)
		if (lt_lstr_eq(slug, chests[i].slug))
			return i;
	return -1;
}

chest_t* chest_add(lt_arena_t* arena, lt_json_t* json) {
	chests = realloc(chests, (chest_count + 1) * sizeof(chest_t));

	usz pfx_len = CLSTR("Chest|").len;
	chests[chest_count].slug = LSTR(json->key.str + pfx_len, json->key.len - pfx_len);
	chests[chest_count].opened_at = -1;

	lstr_t img_slug = lt_json_find_child(json, CLSTR("imageSourceSlug"))->str_val;
	res_load_texture(arena, img_slug, &chests[chest_count].texture);

	return &chests[chest_count++];
}

