// SPDX-License-Identifier: MIT

#include "pluginloader_p.h"

#include <libkern/OSByteOrder.h>
#include <mach-o/fat.h>
#include <mach-o/loader.h>

#include <cstring>
#include <fstream>
#include <limits>
#include <utility>

namespace fs = std::filesystem;

namespace stdc::plugin {

    static constexpr const char *metadataSectionName = "stdc_metadata";

    static bool valid_range(uint64_t offset, uint64_t size, uint64_t fileSize) {
        return offset <= fileSize && size <= fileSize - offset;
    }

    template <class T>
    static bool read_object(std::ifstream &file, uint64_t offset, uint64_t fileSize, T *out) {
        if (!valid_range(offset, sizeof(T), fileSize)) {
            return false;
        }
        file.seekg(static_cast<std::streamoff>(offset));
        file.read(reinterpret_cast<char *>(out), sizeof(T));
        return bool(file);
    }

    static bool read_bytes(std::ifstream &file, uint64_t offset, uint64_t size, uint64_t fileSize,
                           std::string *out) {
        if (!valid_range(offset, size, fileSize) || size > std::numeric_limits<size_t>::max() ||
            size > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max())) {
            return false;
        }
        std::string bytes(static_cast<size_t>(size), '\0');
        file.seekg(static_cast<std::streamoff>(offset));
        file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!file) {
            return false;
        }
        *out = std::move(bytes);
        return true;
    }

    static bool read_macho_slice(std::ifstream &file, uint64_t base, uint64_t fileSize,
                                 std::string *text) {
        uint32_t magic = 0;
        if (!read_object(file, base, fileSize, &magic)) {
            return false;
        }

        uint32_t commands = 0;
        uint32_t commandsSize = 0;
        uint64_t commandOffset = 0;
        if (magic == MH_MAGIC) {
            mach_header header{};
            if (!read_object(file, base, fileSize, &header)) {
                return false;
            }
            commands = header.ncmds;
            commandsSize = header.sizeofcmds;
            commandOffset = base + sizeof(header);
        } else if (magic == MH_MAGIC_64) {
            mach_header_64 header{};
            if (!read_object(file, base, fileSize, &header)) {
                return false;
            }
            commands = header.ncmds;
            commandsSize = header.sizeofcmds;
            commandOffset = base + sizeof(header);
        } else {
            return false;
        }
        if (!valid_range(commandOffset, commandsSize, fileSize) ||
            uint64_t(commands) * sizeof(load_command) > commandsSize) {
            return false;
        }
        auto commandsEnd = commandOffset + commandsSize;

        for (uint32_t i = 0; i < commands; ++i) {
            load_command command{};
            if (!read_object(file, commandOffset, commandsEnd, &command) ||
                command.cmdsize < sizeof(command) ||
                command.cmdsize > commandsEnd - commandOffset) {
                return false;
            }
            if (command.cmd == LC_SEGMENT) {
                segment_command segment{};
                if (command.cmdsize < sizeof(segment) ||
                    !read_object(file, commandOffset, commandsEnd, &segment) ||
                    segment.nsects > (command.cmdsize - sizeof(segment)) / sizeof(section)) {
                    return false;
                }
                for (uint32_t j = 0; j < segment.nsects; ++j) {
                    section sectionHeader{};
                    auto sectionOffset =
                        commandOffset + sizeof(segment) + uint64_t(j) * sizeof(section);
                    if (!read_object(file, sectionOffset, commandsEnd, &sectionHeader)) {
                        return false;
                    }
                    if (std::strncmp(sectionHeader.sectname, metadataSectionName, 16) == 0) {
                        if (sectionHeader.offset > std::numeric_limits<uint64_t>::max() - base) {
                            return false;
                        }
                        return read_bytes(file, base + sectionHeader.offset, sectionHeader.size,
                                          fileSize, text);
                    }
                }
            } else if (command.cmd == LC_SEGMENT_64) {
                segment_command_64 segment{};
                if (command.cmdsize < sizeof(segment) ||
                    !read_object(file, commandOffset, commandsEnd, &segment) ||
                    segment.nsects > (command.cmdsize - sizeof(segment)) / sizeof(section_64)) {
                    return false;
                }
                for (uint32_t j = 0; j < segment.nsects; ++j) {
                    section_64 sectionHeader{};
                    auto sectionOffset =
                        commandOffset + sizeof(segment) + uint64_t(j) * sizeof(section_64);
                    if (!read_object(file, sectionOffset, commandsEnd, &sectionHeader)) {
                        return false;
                    }
                    if (std::strncmp(sectionHeader.sectname, metadataSectionName, 16) == 0) {
                        if (sectionHeader.offset > std::numeric_limits<uint64_t>::max() - base) {
                            return false;
                        }
                        return read_bytes(file, base + sectionHeader.offset, sectionHeader.size,
                                          fileSize, text);
                    }
                }
            }
            commandOffset += command.cmdsize;
        }
        return false;
    }

    bool PluginLoader::Impl::decodeEmbeddedText(const fs::path &filePath, std::string *text,
                                                std::string *errorMessage) {
        std::ifstream file(filePath, std::ios::binary);
        file.seekg(0, std::ios::end);
        auto end = file.tellg();
        if (end < 0) {
            *errorMessage = "could not read the library";
            return false;
        }
        auto fileSize = static_cast<uint64_t>(end);

        uint32_t magic = 0;
        if (!read_object(file, 0, fileSize, &magic)) {
            *errorMessage = "could not read the library";
            return false;
        }
        if (magic == FAT_CIGAM || magic == FAT_CIGAM_64) {
            fat_header header{};
            if (!read_object(file, 0, fileSize, &header)) {
                *errorMessage = "could not read the library";
                return false;
            }
            auto count = OSSwapBigToHostInt32(header.nfat_arch);
            auto archSize = magic == FAT_CIGAM ? sizeof(fat_arch) : sizeof(fat_arch_64);
            if (!valid_range(sizeof(header), uint64_t(count) * archSize, fileSize)) {
                *errorMessage = "contains an invalid universal binary header";
                return false;
            }
            for (uint32_t i = 0; i < count; ++i) {
                uint64_t offset = 0;
                uint64_t size = 0;
                if (magic == FAT_CIGAM) {
                    fat_arch arch{};
                    if (!read_object(file, sizeof(header) + uint64_t(i) * sizeof(arch), fileSize,
                                     &arch)) {
                        break;
                    }
                    offset = OSSwapBigToHostInt32(arch.offset);
                    size = OSSwapBigToHostInt32(arch.size);
                } else {
                    fat_arch_64 arch{};
                    if (!read_object(file, sizeof(header) + uint64_t(i) * sizeof(arch), fileSize,
                                     &arch)) {
                        break;
                    }
                    offset = OSSwapBigToHostInt64(arch.offset);
                    size = OSSwapBigToHostInt64(arch.size);
                }
                if (!valid_range(offset, size, fileSize)) {
                    continue;
                }
                if (read_macho_slice(file, offset, offset + size, text)) {
                    return true;
                }
                file.clear();
            }
        } else if (read_macho_slice(file, 0, fileSize, text)) {
            return true;
        }
        *errorMessage = "does not contain embedded plugin metadata";
        return false;
    }

}
