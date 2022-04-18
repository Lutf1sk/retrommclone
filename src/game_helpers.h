#ifndef GAME_HELPERS_H
#define GAME_HELPERS_H 1

#include <lt/lt.h>
#include <lt/mem.h>
#include <lt/io.h>
#include <lt/str.h>

#include <stdarg.h>

#include "resource.h"

#define DIR_DOWN	0
#define DIR_LEFT	1
#define DIR_RIGHT	2
#define DIR_UP		3

static inline
u8 find_direction(lstr_t str) {
	if (lt_lstr_eq(str, CLSTR("up")))
		return DIR_UP;
	if (lt_lstr_eq(str, CLSTR("down")))
		return DIR_DOWN;
	if (lt_lstr_eq(str, CLSTR("left")))
		return DIR_LEFT;
	if (lt_lstr_eq(str, CLSTR("right")))
		return DIR_RIGHT;
	return DIR_DOWN;
}

static inline
lstr_t asprintf(lt_arena_t* arena, char* fmt, ...) {
	va_list list;
	va_start(list, fmt);
	char* str = lt_arena_reserve(arena, 0);
	isz len = lt_str_vprintf(str, fmt, list);
	lt_arena_reserve(arena, len);
	va_end(list);
	return LSTR(str, len);
}

#endif
