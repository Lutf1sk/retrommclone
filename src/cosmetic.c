#include "cosmetic.h"
#include "game_helpers.h"

#include <lt/json.h>

clothes_color_t* clothes_colors = NULL;
usz clothes_color_count = 0;

hair_color_t* hair_colors = NULL;
usz hair_color_count = 0;

clothes_dye_t* clothes_dyes = NULL;
usz clothes_dye_count = 0;

hair_dye_t* hair_dyes = NULL;
usz hair_dye_count = 0;

mask_t* masks = NULL;
usz mask_count = 0;

outfit_t* outfits = NULL;
usz outfit_count = 0;

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

hair_color_t* find_hair_color(lstr_t slug) {
	for (usz i = 0; i < hair_color_count; ++i)
		if (lt_lstr_eq(slug, hair_colors[i].slug))
			return &hair_colors[i];
	return NULL;
}

clothes_color_t* find_clothes_color(lstr_t slug) {
	for (usz i = 0; i < clothes_color_count; ++i)
		if (lt_lstr_eq(slug, clothes_colors[i].slug))
			return &clothes_colors[i];
	return NULL;
}

hair_dye_t* find_hair_dye(lstr_t slug) {
	for (usz i = 0; i < hair_dye_count; ++i)
		if (lt_lstr_eq(slug, hair_dyes[i].slug))
			return &hair_dyes[i];
	return NULL;
}

clothes_dye_t* find_clothes_dye(lstr_t slug) {
	for (usz i = 0; i < clothes_dye_count; ++i)
		if (lt_lstr_eq(slug, clothes_dyes[i].slug))
			return &clothes_dyes[i];
	return NULL;
}


void clothes_color_add(lt_arena_t* arena, lt_json_t* json) {
	clothes_colors = realloc(clothes_colors, (clothes_color_count + 1) * sizeof(clothes_color_t));

	usz pfx_len = CLSTR("ClothesColor|").len;
	clothes_colors[clothes_color_count].slug = LSTR(json->key.str + pfx_len, json->key.len - pfx_len);
	lstr_t clr1_str = lt_json_find_child(json, CLSTR("color1"))->str_val;
	++clr1_str.str, --clr1_str.len;
	lstr_t clr2_str = lt_json_find_child(json, CLSTR("color2"))->str_val;
	++clr2_str.str, --clr2_str.len;
	clothes_colors[clothes_color_count].clr1 = lt_lstr_hex_uint(clr1_str);
	clothes_colors[clothes_color_count].clr2 = lt_lstr_hex_uint(clr2_str);

	clothes_color_count++;
}

void hair_color_add(lt_arena_t* arena, lt_json_t* json) {
	hair_colors = realloc(hair_colors, (hair_color_count + 1) * sizeof(hair_color_t));

	usz pfx_len = CLSTR("HairColor|").len;
	hair_colors[hair_color_count].slug = LSTR(json->key.str + pfx_len, json->key.len - pfx_len);

	lstr_t clr1_str = lt_json_find_child(json, CLSTR("color1"))->str_val;
	++clr1_str.str, --clr1_str.len;
	lstr_t clr2_str = lt_json_find_child(json, CLSTR("color2"))->str_val;
	++clr2_str.str, --clr2_str.len;
	lstr_t clr3_str = lt_json_find_child(json, CLSTR("color3"))->str_val;
	++clr3_str.str, --clr3_str.len;
	hair_colors[hair_color_count].clr1 = lt_lstr_hex_uint(clr1_str);
	hair_colors[hair_color_count].clr2 = lt_lstr_hex_uint(clr2_str);
	hair_colors[hair_color_count].clr3 = lt_lstr_hex_uint(clr3_str);

	hair_color_count++;
}

void clothes_dye_add(lt_arena_t* arena, lt_json_t* json) {
	clothes_dyes = realloc(clothes_dyes, (clothes_dye_count + 1) * sizeof(clothes_dye_t));

	usz pfx_len = CLSTR("ClothesDye|").len;
	clothes_dyes[clothes_dye_count].slug = LSTR(json->key.str + pfx_len, json->key.len - pfx_len);
	clothes_dyes[clothes_dye_count].primary = find_clothes_color(lt_json_find_child(json, CLSTR("primaryClothesColorSlug"))->str_val);
	clothes_dyes[clothes_dye_count].secondary = find_clothes_color(lt_json_find_child(json, CLSTR("secondaryClothesColorSlug"))->str_val);

	clothes_dye_count++;
}

void hair_dye_add(lt_arena_t* arena, lt_json_t* json) {
	hair_dyes = realloc(hair_dyes, (hair_dye_count + 1) * sizeof(hair_dye_t));

	usz pfx_len = CLSTR("HairDye|").len;
	hair_dyes[hair_dye_count].slug = LSTR(json->key.str + pfx_len, json->key.len - pfx_len);
	hair_dyes[hair_dye_count].color = find_hair_color(lt_json_find_child(json, CLSTR("hairColorSlug"))->str_val);

	hair_dye_count++;
}

void mask_add(lt_arena_t* arena, lt_json_t* json) {
	masks = realloc(masks, (mask_count + 1) * sizeof(mask_t));

	usz pfx_len = CLSTR("Mask|").len;
	masks[mask_count].slug = LSTR(json->key.str + pfx_len, json->key.len - pfx_len);

	lstr_t cosmetic_slug = lt_json_find_child(json, CLSTR("headCosmeticSlug"))->str_val;

	lstr_t img_slug = asprintf(arena, "heads/%S/front/masculine", cosmetic_slug);
	res_load_texture(arena, img_slug, &masks[mask_count].texture_m);

	img_slug = asprintf(arena, "heads/%S/front/feminine", cosmetic_slug);
	res_load_texture(arena, img_slug, &masks[mask_count].texture_f);

	mask_count++;
}

void outfit_add(lt_arena_t* arena, lt_json_t* json) {
	outfits = realloc(outfits, (outfit_count + 1) * sizeof(outfit_t));

	usz pfx_len = CLSTR("Outfit|").len;
	outfits[outfit_count].slug = LSTR(json->key.str + pfx_len, json->key.len - pfx_len);

	lstr_t cosmetic_slug = lt_json_find_child(json, CLSTR("bodyCosmeticSlug"))->str_val;

	lstr_t img_slug = asprintf(arena, "bodies/%S/masculine", cosmetic_slug);
	res_load_texture(arena, img_slug, &outfits[outfit_count].texture_m);

	img_slug = asprintf(arena, "bodies/%S/feminine", cosmetic_slug);
	res_load_texture(arena, img_slug, &outfits[outfit_count].texture_f);

	outfit_count++;
}

