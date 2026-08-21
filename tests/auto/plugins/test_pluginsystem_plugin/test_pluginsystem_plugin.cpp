// SPDX-License-Identifier: MIT

#include <stdcorelib/pluginsystem/iplugin.h>

namespace {

    class TestPluginSystemPlugin : public stdc::pluginsystem::IPlugin {
    public:
        bool initialize(std::string *) override {
            return true;
        }
    };

}

STDC_EXPORT_PLUGIN(TestPluginSystemPlugin)
