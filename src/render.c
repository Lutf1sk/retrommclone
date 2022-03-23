#include "render.h"

#include <lt/window.h>
#include <lt/gui.h>
#include <lt/font.h>
#include <lt/utf8.h>

#include <GL/gl.h>

extern lt_font_t* font;
extern lt_window_t* win;

extern int icons[LT_GUI_ICON_MAX];
extern int glyph_bm;

// Forward declare glGenerateMipmap, since gl.h doesn't (for some reason?)
void glGenerateMipmap(GLint);

void render_init(void) {
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_SCISSOR_TEST);
}

void render_begin(lt_window_t* win) {
	int width, height;
	lt_window_get_size(win, &width, &height);

	glViewport(0, 0, width, height);
	glScissor(0, 0, width, height);

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0.0, width, height, 0.0f, -1.0f, +1.0f);
}

void render_end(lt_window_t* win) {
	lt_window_gl_swap_buffers(win);
	glFinish();
}

void render_create_tex(int w, int h, void* data, GLint* id, u32 flags) {
	glGenTextures(1, id);
	glBindTexture(GL_TEXTURE_2D, *id);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
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
	glBegin(GL_QUADS);
	for (usz i = 0; i < count; ++i) {
		u32 clr = clrs[i];
		u8 cr = (clr >> 16) & 0xFF;
		u8 cg = (clr >> 8) & 0xFF;
		u8 cb = clr & 0xFF;

		glColor3ub(cr, cg, cb); glVertex2f(r[i].x, r[i].y);
		glColor3ub(cr, cg, cb); glVertex2f(r[i].x + r[i].w, r[i].y);
		glColor3ub(cr, cg, cb); glVertex2f(r[i].x + r[i].w, r[i].y + r[i].h);
		glColor3ub(cr, cg, cb); glVertex2f(r[i].x, r[i].y + r[i].h);
	}
	glEnd();
}

void render_draw_text(void* usr, usz count, lt_gui_point_t* pts, lstr_t* strs, u32* clrs) {
	usz w = font->width, h = font->height;

	glBindTexture(GL_TEXTURE_2D, glyph_bm);
	glEnable(GL_TEXTURE_2D);

	float uv_w = 1.0f / font->glyph_count;

	glBegin(GL_QUADS);
	for (usz i = 0; i < count; ++i) {
		u32 clr = clrs[i];
		u8 r = (clr >> 16) & 0xFF;
		u8 g = (clr >> 8) & 0xFF;
		u8 b = clr & 0xFF;

		i32 x = pts[i].x;
		i32 y = pts[i].y;

		lstr_t text = strs[i];
		char* it = text.str, *end = text.str + text.len;
		while (it < end) {
			u32 c;
			it += lt_utf8_decode(&c, it);
			float beg = c * uv_w, end = beg + uv_w;
			glColor3ub(r, g, b); glTexCoord2f(beg, 0.0f); glVertex2f(x, y);
			glColor3ub(r, g, b); glTexCoord2f(end, 0.0f); glVertex2f(x + w, y);
			glColor3ub(r, g, b); glTexCoord2f(end, 1.0f); glVertex2f(x + w, y + h);
			glColor3ub(r, g, b); glTexCoord2f(beg, 1.0f); glVertex2f(x, y + h);

			x += w;
		}
	}
	glEnd();

	glDisable(GL_TEXTURE_2D);
}

void render_draw_icon(void* usr, usz icon, lt_gui_rect_t* r, u32 clr) {
	u8 cr = (clr >> 16) & 0xFF;
	u8 cg = (clr >> 8) & 0xFF;
	u8 cb = clr & 0xFF;

	glBindTexture(GL_TEXTURE_2D, icons[icon]);
	glEnable(GL_TEXTURE_2D);

	glBegin(GL_QUADS);
	glColor3ub(cr, cg, cb); glTexCoord2f(0.0f, 0.0f); glVertex2f(r->x, r->y);
	glColor3ub(cr, cg, cb); glTexCoord2f(1.0f, 0.0f); glVertex2f(r->x + r->w, r->y);
	glColor3ub(cr, cg, cb); glTexCoord2f(1.0f, 1.0f); glVertex2f(r->x + r->w, r->y + r->h);
	glColor3ub(cr, cg, cb); glTexCoord2f(0.0f, 1.0f); glVertex2f(r->x, r->y + r->h);
	glEnd();

	glDisable(GL_TEXTURE_2D);
}

void render_scissor(void* usr, lt_gui_rect_t* r) {
	int width, height;
	lt_window_get_size(win, &width, &height);
	glScissor(r->x, height - (r->y + r->h), r->w, r->h);
}

