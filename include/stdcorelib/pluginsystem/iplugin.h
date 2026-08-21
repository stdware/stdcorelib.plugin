// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGINSYSTEM_IPLUGIN_H
#define STDCORELIB_PLUGINSYSTEM_IPLUGIN_H

#include <string>

#include <stdcorelib/plugin/plugin.h>

namespace stdc::pluginsystem {

    /// The lifecycle interface implemented by a PluginSystem plugin.
    class STDC_PLUGIN_EXPORT IPlugin : public plugin::Plugin {
    public:
        ~IPlugin() override;

        /// Initializes this plugin after all required plugins have been loaded.
        ///
        /// \param errorMessage Receives why initialization failed.
        /// \return Whether initialization succeeded.
        virtual bool initialize(std::string *errorMessage) = 0;

        /// Notifies this plugin after every enabled plugin has finished initialization.
        virtual void pluginInitialized();

        /// Notifies this plugin immediately before the system begins shutting plugins down.
        virtual void aboutToShutdown();
    };

}

#endif // STDCORELIB_PLUGINSYSTEM_IPLUGIN_H
