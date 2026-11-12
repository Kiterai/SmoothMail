/* Client-only ABI adapter for the MIT-licensed upstream XOAUTH2 plugin. */
#include "config.h"
#include <sasl/sasl.h>
#include <sasl/saslplug.h>

extern int xoauth2_client_plug_init(sasl_utils_t *utils, int maxversion, int *version,
                                  sasl_client_plug_t **plugins, int *count);

__declspec(dllexport) int sasl_client_plug_init(const sasl_utils_t *utils, int maxversion,
                                              int *version, sasl_client_plug_t **plugins,
                                              int *count)
{
    return xoauth2_client_plug_init((sasl_utils_t *)utils, maxversion, version, plugins, count);
}
