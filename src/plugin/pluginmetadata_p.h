// SPDX-License-Identifier: MIT

#ifndef STDCORELIB_PLUGIN_PLUGINMETADATA_P_H
#define STDCORELIB_PLUGIN_PLUGINMETADATA_P_H

#include <filesystem>
#include <string>

namespace stdc::plugin {

    /// Reads the metadata bytes embedded in one library without loading its code.
    bool read_embedded_metadata(const std::filesystem::path &filePath, std::string *metadata,
                                std::string *errorMessage);

}

#endif // STDCORELIB_PLUGIN_PLUGINMETADATA_P_H
