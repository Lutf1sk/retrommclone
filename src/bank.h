#ifndef BANK_H
#define BANK_H 1

#include <lt/lt.h>
#include <lt/fwd.h>

typedef
struct bank {
	lstr_t slug;
	i32 opened_at;
	i32 closed_at;
	int texture;
} bank_t;

extern bank_t* banks;
extern usz bank_count;

i8 find_bank_index(lstr_t slug);
bank_t* bank_add(lt_arena_t* arena, lt_json_t* json);

#endif
