// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGIN_STDC_PLUGIN_GLOBAL_H
#define STDCORELIB_PLUGIN_STDC_PLUGIN_GLOBAL_H

#include <stdcorelib/stdc_global.h>

#ifndef STDC_PLUGIN_EXPORT
#  ifdef STDC_PLUGIN_STATIC
#    define STDC_PLUGIN_EXPORT
#  else
#    ifdef STDC_PLUGIN_LIBRARY
#      define STDC_PLUGIN_EXPORT STDC_DECL_EXPORT
#    else
#      define STDC_PLUGIN_EXPORT STDC_DECL_IMPORT
#    endif
#  endif
#endif

#endif // STDCORELIB_PLUGIN_STDC_PLUGIN_GLOBAL_H
