// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGIN_PLUGINCATALOG_H
#define STDCORELIB_PLUGIN_PLUGINCATALOG_H

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <stdcorelib/plugin/pluginfactory.h>

namespace stdc::plugin {

    /// \addtogroup plugin
    /// @{

    /// Owns a plugin factory and indexes one IID by metadata keys.
    ///
    /// The catalog keeps the factory alive and updates its key index after the factory changes.
    /// Loaders remain valid according to the factory's pointer invalidation rules.
    class STDC_PLUGIN_EXPORT PluginCatalog {
    public:
        /// \param iid The extension point to request from \a factory.
        /// \param factory The configured factory whose ownership is transferred to the catalog.
        /// \pre \a iid is not empty and \a factory is not null.
        PluginCatalog(std::string_view iid, std::unique_ptr<PluginFactory> factory);
        virtual ~PluginCatalog();

        PluginCatalog(PluginCatalog &&RHS) noexcept;
        PluginCatalog &operator=(PluginCatalog &&RHS) noexcept;

    public:
        /// The extension point indexed by this catalog.
        const std::string &iid() const;

        /// The factory owned by this catalog.
        PluginFactory *factory() const;

        /// All plugins accepted for this catalog, in factory order.
        const std::vector<PluginLoader *> &loaders() const;

        /// All advertised keys, retaining the first spelling and declaration order.
        const std::vector<std::string> &keys() const;

        /// The first loader advertising \a key, or null when none does.
        PluginLoader *loader(std::string_view key) const;

        /// Every loader advertising \a key, in factory order.
        std::vector<PluginLoader *> loaders(std::string_view key) const;

        /// Loads the first factory advertising \a key and asks it to create a product.
        ///
        /// \tparam PluginInterface The interface returned by the factory's \c create() function.
        /// \tparam FactoryInterface The interface implemented by the loaded plugin instance.
        /// \param key The advertised key and first argument passed to \c create().
        /// \param args The remaining arguments forwarded to \c create().
        /// \return The created product, or null when lookup, loading, conversion, or creation
        ///         fails. Ownership follows the factory interface's contract.
        template <class PluginInterface, class FactoryInterface, class... Args>
        PluginInterface *loadPlugin(std::string_view key, Args &&...args) const {
            auto pluginLoader = loader(key);
            if (!pluginLoader || !pluginLoader->load()) {
                return nullptr;
            }
            auto factory = dynamic_cast<FactoryInterface *>(pluginLoader->plugin());
            if (!factory) {
                return nullptr;
            }
            return factory->create(key, std::forward<Args>(args)...);
        }

    protected:
        /// Extracts the keys advertised by one plugin.
        ///
        /// The default implementation returns the nonempty strings in the root \c keys array.
        /// A missing or malformed field produces no keys. Overrides may use another metadata
        /// layout or return aliases; lookup itself always compares keys case-sensitively.
        ///
        /// \warning This is called while the catalog index is being built. An override must not
        ///          call a query function on this catalog.
        virtual std::vector<std::string> keysFromMetadata(const json::Value &metadata) const;

    private:
        class Impl;
        std::unique_ptr<Impl> _impl;

        STDC_DISABLE_COPY(PluginCatalog)
    };

    /// @}

}

#endif // STDCORELIB_PLUGIN_PLUGINCATALOG_H
