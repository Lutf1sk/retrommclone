#ifndef SEND_H
#define SEND_H 1

#include <lt/lt.h>
#include <lt/fwd.h>

void send_begin(void);
void send_end(lt_socket_t* sock);

void send_chat(lstr_t channel, lstr_t msg);

void send_click(int x, int y);

void send_key_down(char* key);
void send_key_up(char* key);
void send_key(char* key);

#endif
