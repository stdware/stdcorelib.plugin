// SPDX-License-Identifier: MIT

#include "pluginloader_p.h"

#include <cstring>
#include <fstream>
#include <libkern/OSByteOrder.h>
#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <utility>

namespace fs = std::filesystem;

namespace stdc::plugin {

    static constexpr const char *metadataSectionName = "stdc_metadata";

    static bool read_bytes(std::ifstream &file, uint64_t offset, uint64_t size, std::string *out) {
        std::string bytes(static_cast<size_t>(size), '\0');
        file.seekg(static_cast<std::streamoff>(offset));
        file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!file) {
            return false;
        }
        *out = std::move(bytes);
        return true;
    }

    static bool read_macho_slice(std::ifstream &file, uint64_t base, std::string *metadata) {
        uint32_t magic = 0;
        file.seekg(static_cast<std::streamoff>(base));
        file.read(reinterpret_cast<char *>(&magic), sizeof(magic));
        if (!file) {
            return false;
        }

        uint32_t commands = 0;
        uint64_t commandOffset = 0;
        if (magic == MH_MAGIC) {
            mach_header header{};
            file.seekg(static_cast<std::streamoff>(base));
            file.read(reinterpret_cast<char *>(&header), sizeof(header));
            commands = header.ncmds;
            commandOffset = base + sizeof(header);
        } else if (magic == MH_MAGIC_64) {
            mach_header_64 header{};
            file.seekg(static_cast<std::streamoff>(base));
            file.read(reinterpret_cast<char *>(&header), sizeof(header));
            commands = header.ncmds;
            commandOffset = base + sizeof(header);
        } else {
            return false;
        }

        for (uint32_t i = 0; i < commands; ++i) {
            load_command command{};
            file.seekg(static_cast<std::streamoff>(commandOffset));
            file.read(reinterpret_cast<char *>(&command), sizeof(command));
            if (!file || command.cmdsize < sizeof(command)) {
                return false;
            }
            if (command.cmd == LC_SEGMENT) {
                segment_command segment{};
                file.seekg(static_cast<std::streamoff>(commandOffset));
                file.read(reinterpret_cast<char *>(&segment), sizeof(segment));
                for (uint32_t j = 0; j < segment.nsects; ++j) {
                    section sectionHeader{};
                    file.read(reinterpret_cast<char *>(&sectionHeader), sizeof(sectionHeader));
                    if (std::strncmp(sectionHeader.sectname, metadataSectionName, 16) == 0) {
                        return read_bytes(file, base + sectionHeader.offset, sectionHeader.size,
                                          metadata);
                    }
                }
            } else if (command.cmd == LC_SEGMENT_64) {
                segment_command_64 segment{};
                file.seekg(static_cast<std::streamoff>(commandOffset));
                file.read(reinterpret_cast<char *>(&segment), sizeof(segment));
                for (uint32_t j = 0; j < segment.nsects; ++j) {
                    section_64 sectionHeader{};
                    file.read(reinterpret_cast<char *>(&sectionHeader), sizeof(sectionHeader));
                    if (std::strncmp(sectionHeader.sectname, metadataSectionName, 16) == 0) {
                        return read_bytes(file, base + sectionHeader.offset, sectionHeader.size,
                                          metadata);
                    }
                }
            }
            commandOffset += command.cmdsize;
        }
        return false;
    }

    bool PluginLoader::Impl::readEmbeddedMetadata(const fs::path &filePath, std::string *metadata,
                                                  std::string *errorMessage) {
        std::ifstream file(filePath, std::ios::binary);
        uint32_t magic = 0;
        file.read(reinterpret_cast<char *>(&magic), sizeof(magic));
        if (!file) {
            *errorMessage = "could not read the library";
            return false;
        }
        if (magic == FAT_CIGAM || magic == FAT_CIGAM_64) {
            fat_header header{};
            file.seekg(0);
            file.read(reinterpret_cast<char *>(&header), sizeof(header));
            auto count = OSSwapBigToHostInt32(header.nfat_arch);
            for (uint32_t i = 0; i < count; ++i) {
                uint64_t offset = 0;
                if (magic == FAT_CIGAM) {
                    fat_arch arch{};
                    file.read(reinterpret_cast<char *>(&arch), sizeof(arch));
                    offset = OSSwapBigToHostInt32(arch.offset);
                } else {
                    fat_arch_64 arch{};
                    file.read(reinterpret_cast<char *>(&arch), sizeof(arch));
                    offset = OSSwapBigToHostInt64(arch.offset);
                }
                auto next = file.tellg();
                if (read_macho_slice(file, offset, metadata)) {
                    return true;
                }
                file.clear();
                file.seekg(next);
            }
        } else if (read_macho_slice(file, 0, metadata)) {
            return true;
        }
        *errorMessage = "does not contain embedded plugin metadata";
        return false;
    }

}
