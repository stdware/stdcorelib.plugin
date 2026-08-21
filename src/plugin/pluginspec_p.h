// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGIN_PLUGINSPEC_P_H
#define STDCORELIB_PLUGIN_PLUGINSPEC_P_H

#include <stdcorelib/support/sharedlibrary.h>

#include <stdcorelib/plugin/plugin.h>
#include <stdcorelib/plugin/pluginspec.h>

namespace stdc {

    class PluginSpec::Impl {
    public:
        explicit Impl(PluginSpec *decl);
        ~Impl();

        using Decl = PluginSpec;
        PluginSpec *_decl;

    public:
        /// Where the instance is meant to come from.
        ///
        /// The three differ only in what \c loadLibrary() has to do, so everything downstream of
        /// a spec treats them alike.
        enum Origin {
            /// A shared library sitting next to the manifest that described it.
            FileSystem,

            /// Linked into the program, handing over its metadata rather than writing it down.
            Static,

            /// An instance the program built and handed over, already live.
            Runtime,
        };

        Origin origin = FileSystem;

        PluginSpec::State state = PluginSpec::Invalid;
        bool hasError = false;
        std::string errorString;

        std::string iid;
        std::filesystem::path location;
        std::filesystem::path filePath;
        json::Value metadata;

        /// Set for \c Static, which needs a way back to the instance it was registered with.
        StaticPlugin::PluginInstanceFunction staticInstance = nullptr;

        Plugin *plugin = nullptr;
        SharedLibrary *library = nullptr;

    public:
        /// Reads \a manifestPath and fills everything but the instance.
        ///
        /// Failure is recorded rather than thrown away, so that a plugin which is installed but
        /// unusable still shows up with a reason attached.
        bool read(const std::filesystem::path &manifestPath);

        bool loadLibrary();

        /// Records \a err and returns false, so a check can be written as one line.
        bool reportError(std::string err);
    };

}

#endif // STDCORELIB_PLUGIN_PLUGINSPEC_P_H
