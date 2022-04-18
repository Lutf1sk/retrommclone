#ifndef COSMETIC_H
#define COSMETIC_H 1

#include <lt/lt.h>
#include <lt/fwd.h>

typedef
struct clothes_color {
	lstr_t slug;
	u32 clr1;
	u32 clr2;
} clothes_color_t;

extern clothes_color_t* clothes_colors;
extern usz clothes_color_count;

typedef
struct hair_color {
	lstr_t slug;
	u32 clr1;
	u32 clr2;
	u32 clr3;
} hair_color_t;

extern hair_color_t* hair_colors;
extern usz hair_color_count;

typedef
struct clothes_dye {
	lstr_t slug;
	clothes_color_t* primary;
	clothes_color_t* secondary;
} clothes_dye_t;

extern clothes_dye_t* clothes_dyes;
extern usz clothes_dye_count;

typedef
struct hair_dye {
	lstr_t slug;
	hair_color_t* color;
} hair_dye_t;

extern hair_dye_t* hair_dyes;
extern usz hair_dye_count;

typedef
struct mask {
	lstr_t slug;
	int texture_m;
	int texture_f;
} mask_t;

#define MAX_MASKS 256

extern mask_t* masks;
extern usz mask_count;

typedef
struct outfit {
	lstr_t slug;
	int texture_m;
	int texture_f;
} outfit_t;

#define MAX_OUTFITS 256

extern outfit_t* outfits;
extern usz outfit_count;

#define FIGURE_M 0
#define FIGURE_F 0

hair_color_t* find_hair_color(lstr_t slug);
clothes_color_t* find_clothes_color(lstr_t slug);
hair_dye_t* find_hair_dye(lstr_t slug);
clothes_dye_t* find_clothes_dye(lstr_t slug);

mask_t* find_mask(lstr_t slug);
outfit_t* find_outfit(lstr_t slug);

void clothes_color_add(lt_arena_t* arena, lt_json_t* json);
void hair_color_add(lt_arena_t* arena, lt_json_t* json);
void clothes_dye_add(lt_arena_t* arena, lt_json_t* json);
void hair_dye_add(lt_arena_t* arena, lt_json_t* json);

void mask_add(lt_arena_t* arena, lt_json_t* json);
void outfit_add(lt_arena_t* arena, lt_json_t* json);

#endif
