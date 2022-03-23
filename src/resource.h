#ifndef RESOURCE_H
#define RESOURCE_H 1

#include <lt/lt.h>
#include <lt/fwd.h>

b8 res_load(lt_arena_t* arena, lstr_t path, lstr_t* out);

b8 res_load_texture(lt_arena_t* arena, lstr_t slug, int* out);

#endif
