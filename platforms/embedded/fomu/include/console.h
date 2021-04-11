#ifndef __CONSOLE_H
#define __CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*console_write_hook)(char);
typedef char (*console_read_hook)(void);
typedef int (*console_read_nonblock_hook)(void);

void console_set_write_hook(console_write_hook h);
void console_set_read_hook(console_read_hook r, console_read_nonblock_hook rn);

char readchar(void);
int readchar_nonblock(void);

void putsnonl(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* __CONSOLE_H */
