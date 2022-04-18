#ifndef CHAT_H
#define CHAT_H 1

#include <lt/lt.h>
#include <lt/fwd.h>
#include <lt/gui.h>

typedef
struct chatmsg {
	u32 clr;
	lstr_t text;
} chatmsg_t;

#define CHAT_NPC_CLR 0xFFFFAA44
#define CHAT_PLAYER_CLR 0xFF44AAFF
#define CHAT_SERVER_CLR 0xFFAAAAAA

extern chatmsg_t* chat_msgs;
extern int chat_msg_count;

void add_chat_msg(lstr_t msg_str, u32 clr);

#define CHATMSG_MAXLEN 254

extern char tb_buf[CHATMSG_MAXLEN];
extern lt_gui_textbox_state_t tb_state;
extern b8 textbox_selected;

#endif
