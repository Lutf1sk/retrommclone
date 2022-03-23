#ifndef RENDER_H
#define RENDER_H 1

#include <lt/fwd.h>
#include <lt/lt.h>

void render_init(void);

void render_begin(lt_window_t* win);
void render_end(lt_window_t* win);

#define TEXTURE_FILTER 1

void render_create_tex(int w, int h, void* data, int* id, u32 flag);

void render_draw_rect(void* usr, usz count, lt_gui_rect_t* r, u32* clrs);
void render_draw_text(void* usr, usz count, lt_gui_point_t* pts, lstr_t* strs, u32* clrs);
void render_draw_icon(void* usr, usz icon, lt_gui_rect_t* r, u32 clr);

void render_scissor(void* usr, lt_gui_rect_t* r);

#endif
