# STDCORELIB.PLUGIN

Plugin management module.

## Introduction

`stdcorelib-plugin` contains two layers.

- The `stdc::plugin` namespace provides plugin instances, metadata loading, dynamic library loading, static registration, runtime registration, and filesystem discovery.
- The `stdc::pluginsystem` namespace builds an application lifecycle, dependency graph, and enabled-state settings on top of that foundation.

## Plugin Foundation

The base layer treats a plugin as an implementation of an interface identified by an IID. It does not interpret extension-specific metadata.

### Plugin Metadata

A plugin has two independent pieces of descriptive information:

- Its IID is a required non-empty string that identifies the extension point implemented by the plugin.
- Its metadata is a JSON object owned by that extension point. `PluginLoader` does not reserve or interpret any field in it.

For a dynamic plugin, the IID and metadata are embedded in the library using this internal envelope:

```json
{
    "iid": "org.example.Renderer",
    "metadata": {
        "backend": "software"
    }
}
```

The envelope belongs to the loader and is not the user metadata object returned by `PluginLoader::metadata()`. The metadata itself may use fields such as `iid`, `metadata`, or `name` with any meaning defined by the extension point.

The base contract has no schema-version field such as `$version`. An extension point can define and version its metadata when needed.

### Dynamic Plugins

A dynamic plugin derives from `stdc::plugin::Plugin`, normally through an IID-specific interface, and exports one instance with `STDC_EXPORT_PLUGIN`.

```cpp
#include <stdcorelib/plugin/plugin.h>

class Renderer : public stdc::plugin::Plugin {
public:
    virtual void render() = 0;
};

class SoftwareRenderer final : public Renderer {
public:
    void render() override {
    }
};

STDC_EXPORT_PLUGIN(SoftwareRenderer)
```

To embed the IID and metadata:

- Link the plugin target to `stdcorelib::plugin`.
- Call `stdc_add_plugin_metadata()` with the target, IID, and optional metadata path.
- Make the metadata file itself a JSON object containing only user metadata. Omitting `METADATA` embeds an empty object.
- On macOS, let that function give a `MODULE` target the `.dylib` suffix used by plugin discovery.

For example, `software_renderer.json` can contain:

```json
{
    "backend": "software"
}
```

```cmake
add_library(software_renderer MODULE softwarerenderer.cpp)
target_link_libraries(software_renderer PRIVATE stdcorelib::plugin)
stdc_add_plugin_metadata(
    TARGET software_renderer
    IID org.example.Renderer
    METADATA software_renderer.json
)
```

### Direct Loading

`PluginLoader` manages exactly one plugin:

- A filesystem plugin reads its IID and metadata without executing plugin code.
  - `load()` opens the library and obtains its instance.
  - `unload()` releases the library and invalidates the returned pointer.
- A static plugin obtains its registered instance when loaded.
- A runtime plugin wraps an instance already owned by the caller.

```cpp
#include <stdcorelib/plugin/pluginloader.h>

stdc::plugin::PluginLoader loader(pluginPath);
if (loader.iid() == "org.example.Renderer" && loader.load()) {
    auto renderer = dynamic_cast<Renderer *>(loader.plugin());
    if (renderer) {
        renderer->render();
    }
}
loader.unload();
```

The filesystem interface has two additional rules:

- Pass a second path to the constructor or `setFilePath()` to replace the embedded user metadata with an external JSON object. The IID still comes from the library.
- Inspect failed metadata reads and failed loads through `state()`, `hasError()`, and `errorMessage()`.

### Plugin Discovery

`PluginFactory` owns loaders and discovers filesystem plugins lazily for a requested IID. Its default policy:

- Examines dynamic libraries directly below each search path.
- Accepts only libraries with readable embedded plugin information.
- Silently ignores candidates whose IID does not match the requested IID.

```cpp
#include <filesystem>
#include <vector>

#include <stdcorelib/plugin/pluginfactory.h>

stdc::plugin::PluginFactory factory;
const std::vector<std::filesystem::path> paths{"plugins"};
factory.setPluginPaths("org.example.Renderer", paths);

for (auto loader : factory.plugins("org.example.Renderer")) {
    if (!loader->load()) {
        // Report loader->filePath() and loader->errorMessage().
        continue;
    }
    auto renderer = dynamic_cast<Renderer *>(loader->plugin());
    if (renderer) {
        renderer->render();
    }
}
```

Discovery has the following extension and lifetime rules:

- `BundlePluginFactory` provides the common external-metadata layout: every immediate child directory is a bundle, its root `name` selects the platform library name, and its constructor can replace `plugin.json` with another relative metadata path such as `metadata/plugin.json`.
- Subclasses can override `scanPluginPaths()` and `resolvePluginPath()` to implement another directory layout or external metadata policy.
- Replacing search paths discards previously discovered filesystem loaders that are not loaded.
- Loaded plugins are not unloaded when search paths change.
- Programs should set all search paths before the first query or load.

### Static And Runtime Plugins

Static registration is lazy:

- `STDC_EXPORT_STATIC_PLUGIN` registers a static plugin for an IID.
- The metadata is evaluated when it is first requested.
- The plugin instance is created when its loader is loaded.

```cpp
STDC_EXPORT_STATIC_PLUGIN(
    SoftwareRenderer,
    "org.example.Renderer",
    (stdc::json::Object{{"backend", "software"}})
)

stdc::plugin::PluginFactory factory;
factory.addStaticPlugins("org.example.Renderer");
```

For a runtime plugin, the caller creates and owns the instance, then passes its IID and metadata separately:

```cpp
SoftwareRenderer renderer;

stdc::plugin::PluginFactory factory;
factory.addRuntimePlugin(
    "org.example.Renderer",
    &renderer,
    stdc::json::Object{{"backend", "software"}}
);
```

Static and runtime plugins differ only in how their instances enter the factory:

- `addStaticPlugins()` adds registered plugins for an IID. Their instances are created when their loaders are loaded.
- `addRuntimePlugin()` adds an instance already owned by the caller.
- Neither kind can be unloaded because its loader does not control the instance lifetime.

## Plugin System

`stdc::pluginsystem::PluginSystem` adds a fixed application plugin contract to the base layer. Each system:

- Accepts exactly one IID through its constructor.
- Discovers filesystem plugins only.
- Resolves plugin dependencies.
- Applies global and local enabled-state settings.
- Applies a host load predicate for platform and product policy.
- Controls plugin startup and shutdown.

### Plugin System Metadata

`PluginSystem` is a metadata consumer and reserves these root fields:

- `id` is required, identifies the plugin, and must be unique within the system.
- `displayName` is required display text and may repeat.
- `version` is required and gives the current plugin version.
- `compatVersion` is optional and defaults to `version`.
- `enabledByDefault` is optional and defaults to `true`.
- `dependencies` is optional and defaults to an empty array. Each entry uses the reserved `id`, `version`, and `type` fields.

The IID is not part of this object. It comes from the plugin library and must match the IID passed to the `PluginSystem` constructor.

The metadata has the following schema:

```json
{
    "id": "org.example.editor",
    "displayName": "Editor",
    "version": "2.1.0",
    "compatVersion": "2.0.0",
    "enabledByDefault": true,
    "dependencies": [
        {
            "id": "org.example.core",
            "version": "3.0.0",
            "type": "required"
        },
        {
            "id": "org.example.diagnostics",
            "version": "1.0.0",
            "type": "optional"
        }
    ]
}
```

Any other field is user-defined and is not interpreted by `PluginSystem`. The complete object remains available through `PluginSpec::metadata()`. A custom field must not reuse a field required by `PluginSystem` with an incompatible type or meaning; doing so is undefined behavior.

A plugin with compatibility version `C` and current version `V` satisfies a requested version `R` when `C <= R <= V`.

### Plugin Layouts

`PluginSystem` supports three filesystem layouts:

- `Flat` is the default. It puts plugin libraries directly below each search path and reads their embedded metadata.
- `Bundle` gives every plugin a child directory containing its library and a sidecar `plugin.json`. The metadata must contain a root `name` without a platform library prefix or suffix. For example, `"name": "editor"` can resolve to `editor.dll`, `libeditor.dll`, `libeditor.so`, or `libeditor.dylib`.
- `CustomLayout` is reported when the constructor receives a user-provided `PluginFactory`. Its `scanPluginPaths()` and `resolvePluginPath()` overrides can implement recursive packages, another metadata path, or a separate library subdirectory. `PluginSystem` accepts only filesystem plugins returned by this factory.

In Bundle layout, `name` belongs to `BundlePluginFactory`, while `displayName` belongs to `PluginSystem`. They happen to coexist in the same root metadata object and do not describe the same thing.

### Plugin Implementation

Application plugins implement `stdc::pluginsystem::IPlugin` and export their instance:

```cpp
#include <stdcorelib/pluginsystem/iplugin.h>

class EditorPlugin final : public stdc::pluginsystem::IPlugin {
public:
    bool initialize(std::string *errorMessage) override {
        return true;
    }

    void pluginsInitialized() override {
    }

    void aboutToShutdown() override {
    }
};

STDC_EXPORT_PLUGIN(EditorPlugin)
```

### Host Startup And Shutdown

Configure every path, setting, and load predicate before `loadPlugins()`. Loading freezes the discovered plugin set and all configuration, so later changes have no effect.

A typical application treats the two settings files differently:

- The global file belongs to the application installation. An installer normally provides it. If it is absent or unreadable, the application may create and save its global defaults only when that location is writable. If writing there requires administrator or root privileges, the installer must provide the file.
- The local file belongs to the current user. It normally does not exist on first startup, so an empty `PluginSettings` value is the local default.
- After plugin shutdown, the application saves the local settings for the next run.

```cpp
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

#include <stdcorelib/pluginsystem/pluginsettings.h>
#include <stdcorelib/pluginsystem/pluginsystem.h>
#include <stdcorelib/support/json.h>

using namespace stdc::pluginsystem;

std::optional<PluginSettings> readPluginSettings(const std::filesystem::path &path) {
    std::ifstream file(path);
    if (!file) {
        return std::nullopt;
    }

    std::stringstream text;
    text << file.rdbuf();

    stdc::json::ParseError parseError;
    auto value = stdc::json::Value::fromJson(text.str(), true, &parseError);
    if (parseError) {
        return std::nullopt;
    }
    return PluginSettings::fromJson(value);
}

bool writePluginSettings(const std::filesystem::path &path,
                         const PluginSettings &settings) {
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    if (error) {
        return false;
    }

    std::ofstream file(path);
    file << settings.toJson().toJson(4);
    return bool(file);
}

int runApplication(const std::filesystem::path &applicationDir,
                   const std::filesystem::path &userConfigDir) {
    const auto globalSettingsPath = applicationDir / "plugin-settings.json";
    const auto localSettingsPath = userConfigDir / "plugin-settings.json";

    PluginSettings globalSettings;
    if (auto stored = readPluginSettings(globalSettingsPath)) {
        globalSettings = std::move(*stored);
    } else {
        globalSettings.setPluginEnabled("org.example.experimental", true);
        if (!writePluginSettings(globalSettingsPath, globalSettings)) {
            return 1;
        }
    }

    PluginSettings localSettings;
    if (auto stored = readPluginSettings(localSettingsPath)) {
        localSettings = std::move(*stored);
    }

    PluginSystem plugins("org.example.ApplicationPlugin");
    const std::vector<std::filesystem::path> paths{applicationDir / "plugins"};
    plugins.setPluginPaths(paths);
    plugins.setPluginSettings(PluginSystem::Global, globalSettings);
    plugins.setPluginSettings(PluginSystem::Local, localSettings);

    plugins.loadPlugins();
    for (const auto spec : plugins.plugins()) {
        if (spec->hasError()) {
            // Report spec->id() and spec->errorMessage().
        }
    }

    plugins.shutdownPlugins();
    if (!writePluginSettings(localSettingsPath, plugins.pluginSettings(PluginSystem::Local))) {
        return 1;
    }
    return 0;
}
```

The lifecycle has the following ordering and ownership rules:

- Loading and initialization follow dependency order.
- `pluginsInitialized()` and `aboutToShutdown()` run in reverse dependency order.
- Shutdown unloads libraries in reverse dependency order.
- After a spec reaches a loaded lifecycle state, `PluginSpec::plugin()` returns its non-owning `IPlugin` pointer. Hosts can cast it to the IID-specific application interface.
- Successful shutdown clears the plugin pointer when the library is unloaded.
- Repeated lifecycle calls have no effect.
- The `PluginSystem` destructor performs shutdown when the host does not call it explicitly.

### Enabled-State Settings

`PluginSettings` represents one settings source and is independent of file I/O. A host supplies separate values to `PluginSystem`:

- Global settings override the metadata value and produce `enabledByGlobalSettings()`.
- Local settings override the global result and produce the final `isEnabled()`.

Each settings value has the following serialization behavior:

- It converts between in-memory JSON and explicit enabled-state overrides.
- It preserves unknown plugin IDs.
- It rejects duplicate or conflicting IDs.
- Its mutable `userData()` object stores application-defined settings without coupling `PluginSettings` to a particular application schema.

```json
{
    "disabledPlugins": ["org.example.diagnostics"],
    "enabledPlugins": ["org.example.experimental"],
    "userData": {
        "theme": "dark"
    }
}
```

Disabled plugins affect dependency resolution as follows:

- A disabled required dependency makes its dependent plugin invalid.
- A disabled optional dependency is treated as absent.
- A disabled plugin does not count as an error by itself.

### Load Selection

`setPluginLoadPredicate()` lets the host decide whether each valid spec applies to the current environment. The predicate runs once when loading starts and may inspect the complete metadata, including host-defined fields such as an operating-system or application-edition constraint.

```cpp
plugins.setPluginLoadPredicate([&](const PluginSpec &spec) {
    const auto &platform = spec.metadata()["platform"];
    return platform.isNull() ||
           (platform.isString() && platform.toString() == currentPlatform);
});
```

A rejected plugin remains visible through `plugins()`, has `isSelectedForLoad() == false`, and is not erroneous by itself. A selected plugin with a rejected required dependency is invalid; a rejected optional dependency is treated as absent. Enabled-state settings and the predicate are independent, so a plugin loads only when both `isEnabled()` and `isSelectedForLoad()` are true.

## License

MIT. See [LICENSE](LICENSE).
