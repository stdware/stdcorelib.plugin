// SPDX-License-Identifier: MIT

#include <stdcorelib/pluginsystem/iplugin.h>

namespace {

    class TestPluginSystemFailingPlugin : public stdc::pluginsystem::IPlugin {
    public:
        bool initialize(std::string *errorMessage) override {
            *errorMessage = "intentional initialization failure";
            return false;
        }
    };

}

STDC_EXPORT_PLUGIN(TestPluginSystemFailingPlugin)
