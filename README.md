# STDCORELIB.PLUGIN

`stdcorelib-plugin` provides dynamic plugin discovery, embedded or sidecar JSON manifests, dependency-aware application startup, enabled-state settings, and ordered shutdown.

## Plugin manifest

A plugin manifest has an interface ID and an extension-defined metadata object. The format deliberately has no schema-version field such as `$version`.

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
      { "id": "org.example.core", "version": "3.0.0", "type": "required" },
      { "id": "org.example.diagnostics", "version": "1.0.0", "type": "optional" }
    ]
  }
}
```

`id`, `name`, and `version` are required. IDs identify plugins and must be unique; names are display text and may repeat. `compatVersion` defaults to `version`, `enabledByDefault` defaults to `true`, and `dependencies` defaults to an empty array. A plugin with compatibility version `C` and current version `V` satisfies a requested version `R` when `C <= R <= V`.

The default flat layout puts plugin libraries directly below each search path and embeds the manifest in the library:

```cmake
add_library(editor_plugin MODULE editorplugin.cpp)
target_link_libraries(editor_plugin PRIVATE stdcorelib::plugin)
stdc_add_plugin_metadata(editor_plugin editor_plugin.json)
```

The directory layout instead gives every plugin a directory containing its library and a sidecar `plugin.json`. That manifest adds a `binary` field naming the library in the same directory.

## Plugin implementation

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

## Host startup and shutdown

Each `PluginSystem` instance manages one constructor-supplied IID and filesystem plugins only. Configure every path and setting before `loadPlugins()`; later changes have no effect because dependency resolution uses one frozen snapshot.

```cpp
#include <filesystem>
#include <vector>

#include <stdcorelib/pluginsystem/pluginsettings.h>
#include <stdcorelib/pluginsystem/pluginsystem.h>

stdc::pluginsystem::PluginSystem plugins("org.example.ApplicationPlugin");
const std::vector<std::filesystem::path> paths{"plugins"};
plugins.setPluginPaths(paths);

stdc::pluginsystem::PluginSettings globalSettings;
globalSettings.setPluginEnabled("org.example.experimental", true);
plugins.setPluginSettings(stdc::pluginsystem::PluginSystem::Global, globalSettings);
stdc::pluginsystem::PluginSettings localSettings;
localSettings.setPluginEnabled("org.example.diagnostics", false);
plugins.setPluginSettings(stdc::pluginsystem::PluginSystem::Local, localSettings);

plugins.loadPlugins();
for (const auto *spec : plugins.plugins()) {
    if (spec->hasError()) {
        // Report spec->id() and spec->errorMessage().
    }
}

plugins.shutdownPlugins();
```

After a spec reaches the loaded lifecycle states, `PluginSpec::plugin()` returns its non-owning `IPlugin` pointer. Hosts can cast it to the IID-specific application interface; successful shutdown clears the pointer as the library is unloaded.

Loading and initialization follow dependency order. `pluginInitialized()` and `aboutToShutdown()` run in reverse dependency order. Shutdown unloads the libraries, repeated lifecycle calls have no effect, and the `PluginSystem` destructor performs shutdown when the host does not call it explicitly.

## Enabled-state settings

`PluginSettings` represents one settings source and is independent of file I/O. A host supplies separate global and local values to `PluginSystem`: global settings turn the manifest value into the effective `enabledByDefault()`, then local settings produce the final `isEnabled()`. This corresponds to system-wide and per-user settings without coupling the value type to storage paths. Each value converts between an in-memory JSON value and explicit overrides, preserves unknown plugin IDs, and rejects duplicate or conflicting IDs.

```json
{
  "disabled": ["org.example.diagnostics"],
  "enabled": ["org.example.experimental"]
}
```

A disabled required dependency makes its dependent plugin invalid; a disabled optional dependency is treated as absent. Disabled plugins do not count as errors by themselves.
