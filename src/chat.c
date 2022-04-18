#include "chat.h"

#include <lt/mem.h>
#include <lt/gui.h>

chatmsg_t* chat_msgs = NULL;
int chat_msg_count = 0;

char tb_buf[CHATMSG_MAXLEN] = {""};
lt_gui_textbox_state_t tb_state = { tb_buf, CHATMSG_MAXLEN, 0 };
b8 textbox_selected = 0;

void add_chat_msg(lstr_t msg_str, u32 clr) {
	// TODO: Fix this garbage :P
	chat_msgs = realloc(chat_msgs, (chat_msg_count + 1) * sizeof(chatmsg_t));

	chat_msgs[chat_msg_count].text.str = malloc(msg_str.len);
	chat_msgs[chat_msg_count].text.len = msg_str.len;
	memcpy(chat_msgs[chat_msg_count].text.str, msg_str.str, msg_str.len);

	chat_msgs[chat_msg_count].clr = clr;

	chat_msg_count++;
}

