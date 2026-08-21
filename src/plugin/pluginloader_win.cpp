// SPDX-License-Identifier: MIT

#include "pluginloader_p.h"

#include <stdcorelib/platform/windows/stdc_windows.h>

namespace fs = std::filesystem;

namespace stdc::plugin {

    bool PluginLoader::Impl::readEmbeddedMetadata(const fs::path &filePath, std::string *metadata,
                                                  std::string *errorMessage) {
        auto module =
            ::LoadLibraryExW(filePath.c_str(), nullptr,
                             LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE);
        if (!module) {
            *errorMessage = "could not open the library as a resource";
            return false;
        }

        auto resource = ::FindResourceW(module, L"stdc_metadata", MAKEINTRESOURCEW(10));
        if (!resource) {
            *errorMessage = "does not contain embedded plugin metadata";
            ::FreeLibrary(module);
            return false;
        }
        auto loaded = ::LoadResource(module, resource);
        auto data = loaded ? ::LockResource(loaded) : nullptr;
        auto size = ::SizeofResource(module, resource);
        if (!data || !size) {
            *errorMessage = "contains an unreadable plugin metadata resource";
            ::FreeLibrary(module);
            return false;
        }

        metadata->assign(static_cast<const char *>(data), size);
        ::FreeLibrary(module);
        return true;
    }

}
