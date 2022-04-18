#ifndef CHEST_H
#define CHEST_H 1

#include <lt/lt.h>
#include <lt/fwd.h>

typedef
struct chest {
	lstr_t slug;
	i32 opened_at;
	int texture;
} chest_t;

extern chest_t* chests;
extern usz chest_count;

i8 find_chest_index(lstr_t slug);

chest_t* chest_add(lt_arena_t* arena, lt_json_t* json);

#endif
