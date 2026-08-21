// SPDX-License-Identifier: MIT

#include <stdcorelib/plugin/plugin.h>

namespace {

    class LoaderTestPlugin : public stdc::plugin::Plugin {};

}

STDC_EXPORT_PLUGIN(LoaderTestPlugin)
