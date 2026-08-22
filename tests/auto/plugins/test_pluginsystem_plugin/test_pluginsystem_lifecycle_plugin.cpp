// SPDX-License-Identifier: MIT

#include <stdcorelib/pluginsystem/iplugin.h>

#ifndef TEST_PLUGINSYSTEM_LIFECYCLE_NAME
#  error TEST_PLUGINSYSTEM_LIFECYCLE_NAME must name this lifecycle test plugin
#endif

namespace {

    using EventCallback = void (*)(const char *plugin, const char *event);

    EventCallback eventCallback = nullptr;

    class TestPluginSystemLifecyclePlugin : public stdc::pluginsystem::IPlugin {
    public:
        bool initialize(std::string *) override {
            notify("initialize");
            return true;
        }

        void pluginsInitialized() override {
            notify("pluginsInitialized");
        }

        void aboutToShutdown() override {
            notify("aboutToShutdown");
        }

    private:
        static void notify(const char *event) {
            if (eventCallback) {
                eventCallback(TEST_PLUGINSYSTEM_LIFECYCLE_NAME, event);
            }
        }
    };

}

extern "C" STDC_DECL_EXPORT void test_pluginsystem_set_event_callback(EventCallback callback) {
    eventCallback = callback;
}

STDC_EXPORT_PLUGIN(TestPluginSystemLifecyclePlugin)
