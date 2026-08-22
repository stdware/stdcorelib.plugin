# STDCORELIB.PLUGIN

Plugin management module.

## Introduction

`stdcorelib-plugin` contains two layers.

- The `stdc::plugin` namespace provides plugin instances, manifest loading, dynamic library loading, static registration, runtime registration, and filesystem discovery.
- The `stdc::pluginsystem` namespace builds an application lifecycle, dependency graph, and enabled-state settings on top of that foundation.

## Plugin Foundation

The base layer treats a plugin as an implementation of an interface identified by an IID. It does not interpret extension-specific metadata.

### Plugin Manifest

A `PluginLoader` manifest is a JSON object. It reserves two root fields:

- `iid` is required, must be a non-empty string, and identifies the extension point implemented by the plugin.
- `metadata` is optional, must be an object when present, and belongs entirely to the extension point named by `iid`. Its contents are user-defined.

All other root fields are open to the user. `PluginLoader` preserves the complete manifest and does not interpret those additional fields.

```json
{
    "iid": "org.example.Renderer",
    "metadata": {
        "backend": "software"
    }
}
```

The base contract has no schema-version field such as `$version`. An extension point can define and version its own `metadata` object when needed.

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

To embed the manifest:

- Link the plugin target to `stdcorelib::plugin`.
- Call `stdc_add_plugin_manifest()` with the target and manifest path.
- On macOS, let that function give a `MODULE` target the `.dylib` suffix used by plugin discovery.

```cmake
add_library(software_renderer MODULE softwarerenderer.cpp)
target_link_libraries(software_renderer PRIVATE stdcorelib::plugin)
stdc_add_plugin_manifest(software_renderer software_renderer.json)
```

### Direct Loading

`PluginLoader` manages exactly one plugin:

- A filesystem plugin reads its manifest without executing plugin code.
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

- Pass a second path to the constructor or `setFilePath()` to read an external JSON manifest instead of the embedded manifest.
- Inspect failed manifest reads and failed loads through `state()`, `hasError()`, and `errorMessage()`.

### Plugin Discovery

`PluginFactory` owns loaders and discovers filesystem plugins lazily for a requested IID. Its default policy:

- Examines dynamic libraries directly below each search path.
- Accepts only libraries with readable embedded manifests.
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

- Subclasses can override `scanPluginPaths()` and `resolvePluginPath()` to implement another directory layout or external manifest policy.
- Replacing search paths discards previously discovered filesystem loaders that are not loaded.
- Loaded plugins are not unloaded when search paths change.
- Programs should set all search paths before the first query or load.

### Static And Runtime Plugins

Static registration is lazy:

- `STDC_EXPORT_STATIC_PLUGIN` registers a static plugin for an IID.
- The manifest is evaluated when it is first requested.
- The plugin instance is created when its loader is loaded.

```cpp
STDC_EXPORT_STATIC_PLUGIN(
    SoftwareRenderer,
    "org.example.Renderer",
    (stdc::json::Object{
        {"iid", "org.example.Renderer"},
        {"metadata", stdc::json::Object{{"backend", "software"}}},
    })
)

stdc::plugin::PluginFactory factory;
factory.addStaticPlugins("org.example.Renderer");
```

For a runtime plugin, the caller creates and owns the instance, then passes it together with its complete manifest:

```cpp
SoftwareRenderer renderer;

stdc::plugin::PluginFactory factory;
factory.addRuntimePlugin(
    &renderer,
    stdc::json::Object{
        {"iid", "org.example.Renderer"},
        {"metadata", stdc::json::Object{{"backend", "software"}}},
    }
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
- Controls plugin startup and shutdown.

### Plugin System Manifest

`PluginSystem` retains the `PluginLoader` contract and reserves additional fields:

- The root `iid` field is still required and must match the IID passed to the `PluginSystem` constructor.
- The root `metadata` field is required and follows the `PluginSystem` schema below.
- The root `binary` field is reserved by the `Directory` layout and names the plugin library inside its directory.
- Other root fields remain user-defined and are not interpreted by `PluginSystem`.

The `metadata` object has the following schema:

```json
{
    "iid": "org.example.ApplicationPlugin",
    "metadata": {
        "id": "org.example.editor",
        "name": "Editor",
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
}
```

The fields inside `metadata` have these meanings:

- `id` is required, identifies the plugin, and must be unique within the system.
- `name` is required display text and may repeat.
- `version` is required and gives the current plugin version.
- `compatVersion` is optional and defaults to `version`.
- `enabledByDefault` is optional and defaults to `true`.
- `dependencies` is optional and defaults to an empty array. Each entry uses the reserved `id`, `version`, and `type` fields.
- Any other field is user-defined and is not interpreted by `PluginSystem`. The complete object remains available through `PluginSpec::manifest()`.

A plugin with compatibility version `C` and current version `V` satisfies a requested version `R` when `C <= R <= V`.

### Plugin Layouts

`PluginSystem` supports two filesystem layouts:

- `Flat` is the default. It puts plugin libraries directly below each search path and reads their embedded manifests.
- `Directory` gives every plugin a child directory containing its library and a sidecar `plugin.json`. The manifest must contain the reserved root `binary` field naming the library in that directory.

### Plugin Implementation

Application plugins implement `stdc::pluginsystem::IPlugin` and export their instance:

```cpp
#include <stdcorelib/pluginsystem/iplugin.h>

class EditorPlugin final : public stdc::pluginsystem::IPlugin {
public:
    bool initialize(std::string *errorMessage) override {
        return true;
    }

    void pluginInitialized() override {
    }

    void aboutToShutdown() override {
    }
};

STDC_EXPORT_PLUGIN(EditorPlugin)
```

### Host Startup And Shutdown

Configure every path and setting before `loadPlugins()`. Loading freezes the discovered plugin set, paths, and settings, so later changes have no effect.

```cpp
#include <filesystem>
#include <vector>

#include <stdcorelib/pluginsystem/pluginsettings.h>
#include <stdcorelib/pluginsystem/pluginsystem.h>

using namespace stdc::pluginsystem;

PluginSystem plugins("org.example.ApplicationPlugin");
const std::vector<std::filesystem::path> paths{"plugins"};
plugins.setPluginPaths(paths);

PluginSettings globalSettings;
globalSettings.setPluginEnabled("org.example.experimental", true);
plugins.setPluginSettings(PluginSystem::Global, globalSettings);

PluginSettings localSettings;
localSettings.setPluginEnabled("org.example.diagnostics", false);
plugins.setPluginSettings(PluginSystem::Local, localSettings);

plugins.loadPlugins();
for (const auto spec : plugins.plugins()) {
    if (spec->hasError()) {
        // Report spec->id() and spec->errorMessage().
    }
}

plugins.shutdownPlugins();
```

The lifecycle has the following ordering and ownership rules:

- Loading and initialization follow dependency order.
- `pluginInitialized()` and `aboutToShutdown()` run in reverse dependency order.
- Shutdown unloads libraries in reverse dependency order.
- After a spec reaches a loaded lifecycle state, `PluginSpec::plugin()` returns its non-owning `IPlugin` pointer. Hosts can cast it to the IID-specific application interface.
- Successful shutdown clears the plugin pointer when the library is unloaded.
- Repeated lifecycle calls have no effect.
- The `PluginSystem` destructor performs shutdown when the host does not call it explicitly.

### Enabled-State Settings

`PluginSettings` represents one settings source and is independent of file I/O. A host supplies separate values to `PluginSystem`:

- Global settings override the manifest value and produce `enabledByGlobalSettings()`.
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
