// SPDX-License-Identifier: MIT

#include <stdcorelib/plugin/plugin.h>

namespace {

    class LoaderTestPlugin : public stdc::plugin::Plugin {};

}

STDC_EXPORT_PLUGIN(LoaderTestPlugin)

extern "C" STDC_DECL_EXPORT STDC_PLUGIN_METADATA_SECTION const char stdc_plugin_metadata[] = R"({
    "$version": "1.0",
    "iid": "org.stdcorelib.LoaderTest",
    "metadata": {"answer": 42}
})";
