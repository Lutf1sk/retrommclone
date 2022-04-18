#ifndef RENDER_H
#define RENDER_H 1

#include <lt/fwd.h>
#include <lt/lt.h>
#include <lt/linalg.h>

typedef
struct mesh {
	lt_vec3_t* verts;
	lt_vec2_t* uvs;
	lt_vec4_t* clrs;
	u32 vert_count;

	u32 bufs[3];
	u32 vao;
} mesh_t;

void render_init(void);

void render_begin(lt_window_t* win);
void render_end(lt_window_t* win);

void render_upload_mesh(mesh_t* mesh);
void render_free_mesh(mesh_t* mesh);
void render_mesh(mesh_t* mesh, int tex);

#define TEXTURE_FILTER 1

extern int no_tex;

void render_create_tex(int w, int h, void* data, int* id, u32 flag);

void render_draw_rect(void* usr, usz count, lt_gui_rect_t* r, u32* clrs);
void render_draw_text(void* usr, usz count, lt_gui_point_t* pts, lstr_t* strs, u32* clrs);
void render_draw_icon(void* usr, usz icon, lt_gui_rect_t* r, u32 clr);

void render_scissor(void* usr, lt_gui_rect_t* r);

#endif
