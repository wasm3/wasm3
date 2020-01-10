//
//  Use this file to import your target's public headers that you would like to expose to Swift.
//

typedef void (*print_cbk_t)(const char* buff, int len);

void    redirect_output     (print_cbk_t f);
int     run_app             (void);
