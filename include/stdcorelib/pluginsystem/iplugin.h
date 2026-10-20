// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGINSYSTEM_IPLUGIN_H
#define STDCORELIB_PLUGINSYSTEM_IPLUGIN_H

#include <filesystem>
#include <string>
#include <vector>

#include <stdcorelib/plugin/plugin.h>

namespace stdc::pluginsystem {

    /// \addtogroup plugin
    /// @{

    /// The lifecycle interface implemented by a PluginSystem plugin.
    class STDC_PLUGIN_EXPORT IPlugin : public plugin::Plugin {
    public:
        ~IPlugin() override;

        /// Initializes this plugin after all required plugins have been loaded.
        ///
        /// \param errorMessage Receives why initialization failed.
        /// \return Whether initialization succeeded.
        virtual bool initialize(std::string *errorMessage) = 0;

        /// Notifies this plugin after the initialization pass completes.
        virtual void pluginsInitialized();

        /// Notifies this plugin during shutdown before any plugin library is unloaded.
        virtual void aboutToShutdown();

        /// Handles the arguments that another process of the application passes to this
        /// process instead of running itself, such as the files of a second start.
        ///
        /// \param arguments The arguments in UTF-8, without the program.
        /// \param workingDirectory The working directory of the other process, against which a
        ///        relative path in \a arguments resolves.
        ///
        /// \sa PluginSystem::remoteCommand()
        virtual void remoteCommand(const std::vector<std::string> &arguments,
                                   const std::filesystem::path &workingDirectory);
    };

    /// @}

}

#endif // STDCORELIB_PLUGINSYSTEM_IPLUGIN_H
