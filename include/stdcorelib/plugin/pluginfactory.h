// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGIN_PLUGINFACTORY_H
#define STDCORELIB_PLUGIN_PLUGINFACTORY_H

#include <filesystem>
#include <memory>
#include <vector>

#include <stdcorelib/adt/array_view.h>
#include <stdcorelib/stdc_global.h>

#include <stdcorelib/plugin/pluginspec.h>

namespace stdc {

    /// \addtogroup plugin
    /// @{

    class Plugin;

    class StaticPlugin;

    /// Finds plugins and hands out what is known about them.
    ///
    /// The factory knows three things about a plugin: which extension point it plugs into, where
    /// it lives, and the metadata blob it carries. It does not know what that blob means, and it
    /// must not learn, because the extension points are not its to enumerate. They are declared
    /// by whoever is being extended, in libraries this one has never heard of, so any rule baked
    /// in here would already be wrong for one of them.
    ///
    /// So the factory hands over the candidates and whoever owns the extension point chooses.
    ///
    /// Plugins reach it three ways:
    ///  - filesystem plugins: a directory per plugin, holding a plugin.json and the library
    ///  - static plugins    : linked into the program, handing over the same metadata directly
    ///  - runtime plugins   : instances the program supplies, owned by the program
    class STDC_PLUGIN_EXPORT PluginFactory {
    public:
        PluginFactory();
        virtual ~PluginFactory();

    public:
        static std::vector<std::string> staticPluginSets();
        static std::vector<StaticPlugin> staticPlugins(const char *pluginSet);

    public:
        /// Takes the statically linked plugins registered under \a pluginSet into this factory.
        ///
        /// Which extension point each one plugs into comes out of its metadata, exactly as it
        /// would for a plugin on disk.
        void addStaticPlugins(const char *pluginSet);

        /// Takes an instance the program already holds. Ownership stays with the program.
        ///
        /// \a metadata says what a plugin.json would have said, \c iid included.
        void addRuntimePlugin(Plugin *plugin, const json::Value &metadata);

    public:
        /// Adds a directory to search for \a iid. Each subdirectory holding a plugin.json is one
        /// plugin.
        void addPluginPath(const char *iid, const std::filesystem::path &path);
        void setPluginPaths(const char *iid, array_view<std::filesystem::path> paths);
        std::vector<std::filesystem::path> pluginPaths(const char *iid) const;

    public:
        /// Everything found for \a iid, scanning the registered directories if they have not been
        /// scanned since they last changed.
        ///
        /// Nothing is loaded. The specs of plugins that could not be read are returned along with
        /// the rest, carrying the reason, so that a caller which finds no match can say whether
        /// the plugin it wanted is missing or merely broken.
        std::vector<PluginSpec *> plugins(const char *iid) const;

    protected:
        class Impl;
        std::unique_ptr<Impl> _impl;

        explicit PluginFactory(Impl &impl);

        STDC_DISABLE_COPY_MOVE(PluginFactory)
    };

    /// @}

}

#endif // STDCORELIB_PLUGIN_PLUGINFACTORY_H
