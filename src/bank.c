#include "bank.h"
#include "resource.h"

#include <lt/str.h>
#include <lt/json.h>
#include <lt/mem.h>

bank_t* banks = NULL;
usz bank_count = 0;

i8 find_bank_index(lstr_t slug) {
	for (usz i = 0; i < bank_count; ++i)
		if (lt_lstr_eq(slug, banks[i].slug))
			return i;
	return -1;
}

bank_t* bank_add(lt_arena_t* arena, lt_json_t* json) {
	banks = realloc(banks, (bank_count + 1) * sizeof(bank_t));

	usz pfx_len = CLSTR("Bank|").len;
	banks[bank_count].slug = LSTR(json->key.str + pfx_len, json->key.len - pfx_len);
	banks[bank_count].opened_at = -1;
	banks[bank_count].closed_at = -1;

	lstr_t img_slug = lt_json_find_child(json, CLSTR("imageSourceSlug"))->str_val;
	res_load_texture(arena, img_slug, &banks[bank_count].texture);

	return &banks[bank_count++];
}
