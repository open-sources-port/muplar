#include <stddef.h>

void apex_adbconnection_client_set_current_process_name_noop(const char *name)
{
    (void) name;
}
void *adbconnection_client_new(void)
{
    return NULL;
}
void adbconnection_client_destroy(void *ctx)
{
    (void) ctx;
}
