#include "render.h"

#include <lt/window.h>
#include <lt/gui.h>
#include <lt/font.h>
#include <lt/utf8.h>
#include <lt/gl.h>
#include <lt/mem.h>

extern lt_font_t* font;
extern lt_window_t* win;

extern lt_arena_t* render_arena;

extern int icons[LT_GUI_ICON_MAX];
extern int glyph_bm;

int no_tex;

static u32 prog;

static
void compile_shaders(void) {
	const char* vert_src =
		"#version 330\n"
		"layout(location = 0) in vec3 pos;"
		"layout(location = 1) in vec4 clr;"
		"layout(location = 2) in vec2 uv;"
		"varying vec4 frag_clr;"
		"varying vec2 frag_uv;"
		"uniform mat4 projection;"
		"void main() {"
		"	frag_uv = uv;"
		"	frag_clr = clr;"
		"	gl_Position = projection * vec4(pos, 1.0f);"
		"}";

	const char* frag_src =
		"#version 330\n"
		"varying vec4 frag_clr;"
		"varying vec2 frag_uv;"
		"uniform sampler2D tex;"
		"void main() {"
		"	gl_FragColor = frag_clr * texture(tex, frag_uv);"
		"}";

	u32	vert_shader = glCreateShader(GL_VERTEX_SHADER),
		frag_shader = glCreateShader(GL_FRAGMENT_SHADER);

	glShaderSource(vert_shader, 1, &vert_src, NULL);
	glShaderSource(frag_shader, 1, &frag_src, NULL);

	glCompileShader(vert_shader);
	glCompileShader(frag_shader);

	int compiled = 0;
	glGetShaderiv(vert_shader, GL_COMPILE_STATUS, &compiled);
	if (compiled == GL_FALSE) {
		int log_len = 0;
		glGetShaderiv(vert_shader, GL_INFO_LOG_LENGTH, &log_len);
		char* log = lt_arena_reserve(render_arena, log_len);
		glGetShaderInfoLog(vert_shader, log_len, &log_len, log);

		lt_ferrf("Failed to compile vertex shader: '%s'\n", log);
	}
	glGetShaderiv(frag_shader, GL_COMPILE_STATUS, &compiled);
	if (compiled == GL_FALSE) {
		int log_len = 0;
		glGetShaderiv(frag_shader, GL_INFO_LOG_LENGTH, &log_len);
		char* log = lt_arena_reserve(render_arena, log_len);
		glGetShaderInfoLog(frag_shader, log_len, &log_len, log);

		lt_ferrf("Failed to compile fragment shader: %s", log);
	}

	prog = glCreateProgram();
	glAttachShader(prog, vert_shader);
	glAttachShader(prog, frag_shader);
	glLinkProgram(prog);

	int linked = 0;
	glGetProgramiv(prog, GL_LINK_STATUS, &linked);
	if (linked == GL_FALSE) {
		int log_len = 0;
		glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &log_len);
		char* log = lt_arena_reserve(render_arena, log_len);
		glGetProgramInfoLog(prog, log_len, &log_len, log);

		lt_ferrf("Failed to link shader program: %s", log);
	}

	glDetachShader(prog, vert_shader);
	glDetachShader(prog, frag_shader);
}

void render_init(void) {
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_SCISSOR_TEST);

	compile_shaders();

	render_create_tex(1, 1, (u32[]){0xFFFFFFFF}, &no_tex, 0);
}

#include <stdio.h>

void render_begin(lt_window_t* win) {
	int width, height;
	lt_window_get_size(win, &width, &height);

	glViewport(0, 0, width, height);

	lt_mat4_t projection_mat;
	lt_mat4_ortho(projection_mat, 0.0f, width, height, 0.0f, -1.0f, 1.0f);
	int projection_id = glGetUniformLocation(prog, "projection");
	glUniformMatrix4fv(projection_id, 1, GL_FALSE, projection_mat[0]);

	int tex_id = glGetUniformLocation(prog, "tex");
	glUniform1i(tex_id, 0);
// 	printf("(%f, %f, %f, %f)\n", projection_mat[0][0], projection_mat[0][1], projection_mat[0][2], projection_mat[0][3]);
// 	printf("(%f, %f, %f, %f)\n", projection_mat[1][0], projection_mat[1][1], projection_mat[1][2], projection_mat[1][3]);
// 	printf("(%f, %f, %f, %f)\n", projection_mat[2][0], projection_mat[2][1], projection_mat[2][2], projection_mat[2][3]);
// 	printf("(%f, %f, %f, %f)\n", projection_mat[3][0], projection_mat[3][1], projection_mat[3][2], projection_mat[3][3]);
// 	printf("\n");

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glUseProgram(prog);
}

void render_end(lt_window_t* win) {
	lt_window_gl_swap_buffers(win);
	glFinish();
}

void render_upload_mesh(mesh_t* mesh) {
	glGenVertexArrays(1, &mesh->vao);
	glBindVertexArray(mesh->vao);

	glGenBuffers(3, mesh->bufs);

	glBindBuffer(GL_ARRAY_BUFFER, mesh->bufs[0]);
	glBufferData(GL_ARRAY_BUFFER, mesh->vert_count * sizeof(lt_vec3_t), mesh->verts, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(lt_vec3_t), NULL);
	glEnableVertexAttribArray(0);

	glBindBuffer(GL_ARRAY_BUFFER, mesh->bufs[1]);
	glBufferData(GL_ARRAY_BUFFER, mesh->vert_count * sizeof(lt_vec4_t), mesh->clrs, GL_STATIC_DRAW);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(lt_vec4_t), NULL);
	glEnableVertexAttribArray(1);

	glBindBuffer(GL_ARRAY_BUFFER, mesh->bufs[2]);
	glBufferData(GL_ARRAY_BUFFER, mesh->vert_count * sizeof(lt_vec2_t), mesh->uvs, GL_STATIC_DRAW);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(lt_vec2_t), NULL);
	glEnableVertexAttribArray(2);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}

void render_free_mesh(mesh_t* mesh) {
	glDeleteBuffers(3, mesh->bufs);
	glDeleteVertexArrays(1, &mesh->vao);
}

void render_mesh(mesh_t* mesh, int tex) {
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex);
	glBindVertexArray(mesh->vao);
	glDrawArrays(GL_TRIANGLES, 0, mesh->vert_count);
	glBindVertexArray(0);
}

void render_create_tex(int w, int h, void* data, GLint* id, u32 flags) {
	glGenTextures(1, id);
	glBindTexture(GL_TEXTURE_2D, *id);

	static float border_clr[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border_clr);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

	if (flags & TEXTURE_FILTER) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}
	else {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	glGenerateMipmap(GL_TEXTURE_2D);
}

void render_draw_rect(void* usr, usz count, lt_gui_rect_t* r, u32* clrs) {
	lt_arestore_t arestore = lt_arena_save(render_arena);

	u32 vert_count = count * 6;

	mesh_t m;
	m.vert_count = vert_count;
	m.verts = lt_arena_reserve(render_arena, vert_count * sizeof(lt_vec3_t));
	m.clrs = lt_arena_reserve(render_arena, vert_count * sizeof(lt_vec4_t));
	m.uvs = lt_arena_reserve(render_arena, vert_count * sizeof(lt_vec2_t));

	lt_vec2_t vuv = LT_VEC2_INIT(0.0f, 0.0f);

	usz vi = 0;
	for (usz i = 0; i < count; ++i) {
		u32 clr = clrs[i];
		u8 ca = clr >> 24;
		u8 cr = (clr >> 16) & 0xFF;
		u8 cg = (clr >> 8) & 0xFF;
		u8 cb = clr & 0xFF;

		lt_vec4_t vclr = LT_VEC4_INIT(cr, cg, cb, ca);
		lt_vec4_div_f(vclr, 255.0f, vclr);

		float x1 = r[i].x, y1 = r[i].y;
		float x2 = x1 + r[i].w, y2 = y1 + r[i].h;

		lt_vec3_copy(m.verts[vi], LT_VEC3(x1, y1, 0.0f));
		lt_vec4_copy(m.clrs[vi], vclr);
		lt_vec2_copy(m.uvs[vi++], vuv);
		lt_vec3_copy(m.verts[vi], LT_VEC3(x2, y1, 0.0f));
		lt_vec4_copy(m.clrs[vi], vclr);
		lt_vec2_copy(m.uvs[vi++], vuv);
		lt_vec3_copy(m.verts[vi], LT_VEC3(x2, y2, 0.0f));
		lt_vec4_copy(m.clrs[vi], vclr);
		lt_vec2_copy(m.uvs[vi++], vuv);

		lt_vec3_copy(m.verts[vi], LT_VEC3(x1, y1, 0.0f));
		lt_vec4_copy(m.clrs[vi], vclr);
		lt_vec2_copy(m.uvs[vi++], vuv);
		lt_vec3_copy(m.verts[vi], LT_VEC3(x2, y2, 0.0f));
		lt_vec4_copy(m.clrs[vi], vclr);
		lt_vec2_copy(m.uvs[vi++], vuv);
		lt_vec3_copy(m.verts[vi], LT_VEC3(x1, y2, 0.0f));
		lt_vec4_copy(m.clrs[vi], vclr);
		lt_vec2_copy(m.uvs[vi++], vuv);
	}

	render_upload_mesh(&m);
	render_mesh(&m, no_tex);
	render_free_mesh(&m);

	lt_arena_restore(render_arena, &arestore);
}

void render_draw_text(void* usr, usz count, lt_gui_point_t* pts, lstr_t* strs, u32* clrs) {
	lt_arestore_t arestore = lt_arena_save(render_arena);
	isz w = font->width, h = font->height;
	float uv_w = 1.0f / font->glyph_count;

	u32 vert_count = 0;
	for (usz i = 0; i < count; ++i)
		vert_count += strs[i].len;
	vert_count *= 6;

	mesh_t m;
	m.verts = lt_arena_reserve(render_arena, vert_count * sizeof(lt_vec3_t));
	m.clrs = lt_arena_reserve(render_arena, vert_count * sizeof(lt_vec4_t));
	m.uvs = lt_arena_reserve(render_arena, vert_count * sizeof(lt_vec2_t));

	usz vi = 0;
	for (usz i = 0; i < count; ++i) {
		u32 clr = clrs[i];
		u8 a = clr >> 24;
		u8 r = (clr >> 16) & 0xFF;
		u8 g = (clr >> 8) & 0xFF;
		u8 b = clr & 0xFF;

		lt_vec4_t vclr = LT_VEC4_INIT(r, g, b, a);
		lt_vec4_div_f(vclr, 255.0f, vclr);

		float x1 = pts[i].x, y1 = pts[i].y, y2 = y1 + h;

		lstr_t text = strs[i];
		char* it = text.str, *end = text.str + text.len;
		while (it < end) {
			u32 c;
			it += lt_utf8_decode(&c, it);
			float beg = c * uv_w, end = beg + uv_w;

			float x2 = x1 + w;

			lt_vec3_copy(m.verts[vi], LT_VEC3(x1, y1, 0.0f));
			lt_vec4_copy(m.clrs[vi], vclr);
			lt_vec2_copy(m.uvs[vi++], LT_VEC2(beg, 0.0f));
			lt_vec3_copy(m.verts[vi], LT_VEC3(x2, y1, 0.0f));
			lt_vec4_copy(m.clrs[vi], vclr);
			lt_vec2_copy(m.uvs[vi++], LT_VEC2(end, 0.0f));
			lt_vec3_copy(m.verts[vi], LT_VEC3(x2, y2, 0.0f));
			lt_vec4_copy(m.clrs[vi], vclr);
			lt_vec2_copy(m.uvs[vi++], LT_VEC2(end, 1.0f));

			lt_vec3_copy(m.verts[vi], LT_VEC3(x1, y1, 0.0f));
			lt_vec4_copy(m.clrs[vi], vclr);
			lt_vec2_copy(m.uvs[vi++], LT_VEC2(beg, 0.0f));
			lt_vec3_copy(m.verts[vi], LT_VEC3(x2, y2, 0.0f));
			lt_vec4_copy(m.clrs[vi], vclr);
			lt_vec2_copy(m.uvs[vi++], LT_VEC2(end, 1.0f));
			lt_vec3_copy(m.verts[vi], LT_VEC3(x1, y2, 0.0f));
			lt_vec4_copy(m.clrs[vi], vclr);
			lt_vec2_copy(m.uvs[vi++], LT_VEC2(beg, 1.0f));

			x1 += w;
		}
	}
	m.vert_count = vi;

	render_upload_mesh(&m);
	render_mesh(&m, glyph_bm);
	render_free_mesh(&m);

	lt_arena_restore(render_arena, &arestore);
}

void render_draw_icon(void* usr, usz icon, lt_gui_rect_t* rect, u32 clr) {
	float a = (float)(clr >> 24) / 255.0f;
	float r = (float)((clr >> 16) & 0xFF) / 255.0f;
	float g = (float)((clr >> 8) & 0xFF) / 255.0f;
	float b = (float)(clr & 0xFF) / 255.0f;

	float x1 = rect->x, y1 = rect->y;
	float x2 = x1 + rect->w, y2 = y1 + rect->h;

	lt_vec3_t verts[6] = {
		{x1, y1, 0.0f},
		{x2, y1, 0.0f},
		{x2, y2, 0.0f},
		{x1, y1, 0.0f},
		{x2, y2, 0.0f},
		{x1, y2, 0.0f},
	};
	lt_vec4_t clrs[6] = {
		{r, g, b, a},
		{r, g, b, a},
		{r, g, b, a},
		{r, g, b, a},
		{r, g, b, a},
		{r, g, b, a},
	};
	lt_vec2_t uvs[6] = {
		{0.0f, 0.0f},
		{1.0f, 0.0f},
		{1.0f, 1.0f},
		{0.0f, 0.0f},
		{1.0f, 1.0f},
		{0.0f, 1.0f},
	};

	mesh_t m;
	m.verts = verts;
	m.clrs = clrs;
	m.uvs = uvs;
	m.vert_count = 6;

	render_upload_mesh(&m);
	render_mesh(&m, icons[icon]);
	render_free_mesh(&m);
}

void render_scissor(void* usr, lt_gui_rect_t* r) {
	int width, height;
	lt_window_get_size(win, &width, &height);
	if (!r)
		glScissor(0, 0, width, height);
	else
		glScissor(r->x, height - (r->y + r->h), r->w, r->h);
}

