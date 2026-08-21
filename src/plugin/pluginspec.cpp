// SPDX-License-Identifier: MIT

#include "pluginspec.h"
#include "pluginspec_p.h"

#include <fstream>
#include <sstream>
#include <utility>

#include <stdcorelib/path.h>
#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>

namespace fs = std::filesystem;

namespace stdc {

    /// The manifest format this build understands.
    ///
    /// Read before anything else, so that a manifest written for a later version is turned away
    /// rather than half understood.
    static constexpr const char *manifestVersion = "1.0";

    PluginSpec::Impl::Impl(PluginSpec *decl) : _decl(decl) {
    }

    PluginSpec::Impl::~Impl() {
        // A library is unloaded with the spec that opened it. Static and runtime plugins are
        // owned by whoever registered them and are only borrowed here.
        delete library;
    }

    bool PluginSpec::Impl::reportError(std::string err) {
        errorString = std::move(err);
        hasError = true;
        state = PluginSpec::Invalid;
        return false;
    }

    bool PluginSpec::Impl::read(const std::filesystem::path &manifestPath) {
        location = manifestPath.parent_path();

        std::ifstream file(manifestPath);
        if (!file.is_open()) {
            return reportError(formatN(R"(failed to open "%1")", manifestPath));
        }

        std::stringstream ss;
        ss << file.rdbuf();

        json::ParseError parseError;
        auto root = json::Value::fromJson(ss.str(), true, &parseError);
        if (parseError) {
            return reportError(formatN(R"(%1: %2)", manifestPath, parseError.message()));
        }
        if (!root.isObject()) {
            return reportError(formatN(R"(%1: not a JSON object)", manifestPath));
        }
        const auto &obj = root.toObject();

        const auto stringField = [&obj](const std::string_view &key,
                                        std::string_view *out) -> bool {
            auto it = obj.find(key);
            if (it == obj.end() || !it->second.isString()) {
                return false;
            }
            *out = it->second.toString();
            return !out->empty();
        };

        std::string_view version;
        if (!stringField("$version", &version)) {
            return reportError(formatN(R"(%1: missing or invalid "$version" field)", manifestPath));
        }
        if (version != manifestVersion) {
            return reportError(
                formatN(R"(%1: unsupported manifest version "%2")", manifestPath, version));
        }

        std::string_view iid_;
        if (!stringField("iid", &iid_)) {
            return reportError(formatN(R"(%1: missing or invalid "iid" field)", manifestPath));
        }
        iid = iid_;

        std::string_view binary;
        if (!stringField("binary", &binary)) {
            return reportError(formatN(R"(%1: missing or invalid "binary" field)", manifestPath));
        }
        filePath = location / path::from_utf8(binary);
        if (!fs::is_regular_file(filePath)) {
            return reportError(
                formatN(R"(%1: "binary" names "%2", which is not there)", manifestPath, filePath));
        }

        // Whatever is in here belongs to the extension point named by iid. Check that it is an
        // object and leave it alone otherwise, an absent one meaning an empty one.
        if (auto it = obj.find("metadata"); it != obj.end()) {
            if (!it->second.isObject()) {
                return reportError(
                    formatN(R"(%1: "metadata" field is not an object)", manifestPath));
            }
            metadata = it->second;
        } else {
            metadata = json::Object();
        }

        state = PluginSpec::Read;
        return true;
    }

    bool PluginSpec::Impl::loadLibrary() {
        if (hasError) {
            return false;
        }
        if (state == PluginSpec::Loaded) {
            return true;
        }

        if (origin == Static) {
            plugin = staticInstance ? staticInstance() : nullptr;
            if (!plugin) {
                return reportError("static plugin produced no instance");
            }
            state = PluginSpec::Loaded;
            return true;
        }

        auto so = std::make_unique<SharedLibrary>();
        // TODO: ask for the library's own directory to be searched for its dependencies once
        // stdcorelib grows a hint for it, so that a plugin can ship private libraries beside it.
        if (!so->open(filePath)) {
            return reportError(formatN(R"(%1: %2)", filePath, so->errorMessage()));
        }

        auto getter = reinterpret_cast<Plugin *(*) ()>(so->resolve(STDC_PLUGIN_INSTANCE_SYMBOL));
        if (!getter) {
            return reportError(
                formatN(R"(%1: does not export "%2")", filePath, STDC_PLUGIN_INSTANCE_SYMBOL));
        }

        plugin = getter();
        if (!plugin) {
            return reportError(formatN(R"(%1: exported no instance)", filePath));
        }

        library = so.release();
        state = PluginSpec::Loaded;
        return true;
    }

    PluginSpec::PluginSpec(Impl &impl) : _impl(&impl) {
    }

    PluginSpec::~PluginSpec() = default;

    PluginSpec::State PluginSpec::state() const {
        stdc_impl_t;
        return impl.state;
    }

    bool PluginSpec::hasError() const {
        stdc_impl_t;
        return impl.hasError;
    }

    const std::string &PluginSpec::errorString() const {
        stdc_impl_t;
        return impl.errorString;
    }

    const std::string &PluginSpec::iid() const {
        stdc_impl_t;
        return impl.iid;
    }

    const std::filesystem::path &PluginSpec::location() const {
        stdc_impl_t;
        return impl.location;
    }

    const std::filesystem::path &PluginSpec::filePath() const {
        stdc_impl_t;
        return impl.filePath;
    }

    const json::Value &PluginSpec::metadata() const {
        stdc_impl_t;
        return impl.metadata;
    }

    bool PluginSpec::load() {
        stdc_impl_t;
        return impl.loadLibrary();
    }

    Plugin *PluginSpec::plugin() const {
        stdc_impl_t;
        return impl.plugin;
    }

}
