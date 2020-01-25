#include "message_dialog.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char messages[40][512];
static int line = 0;
static int usedLines = 0;


int writeLine(const char *msg, ...) {
    va_list list;
    char string[512];

    va_start(list, msg);
    vsprintf(string, msg, list);
    va_end(list);

    strcpy(messages[line++], string);
    line %= 40;
    usedLines++;
    usedLines = usedLines > 40 ? 40 : usedLines;
    
    return 0;
}

void write_to_screen() {
    vita2d_start_drawing();
    vita2d_clear_screen();
    for(int i = 0; i < usedLines; i++) {
        vita2d_pgf_draw_text(pgf, 10, 20 * (i+1), RGBA8(255, 0, 0, 255), 1.0f, messages[i]);
    }
    vita2d_end_drawing();
    vita2d_swap_buffers();
}