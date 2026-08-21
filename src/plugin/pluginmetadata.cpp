// SPDX-License-Identifier: MIT

#include "pluginmetadata_p.h"

#include <cstring>
#include <fstream>
#include <vector>

#ifdef _WIN32
#  include <stdcorelib/platform/windows/stdc_windows.h>
#elif defined(__APPLE__)
#  include <libkern/OSByteOrder.h>
#  include <mach-o/fat.h>
#  include <mach-o/loader.h>
#else
#  include <elf.h>
#endif

namespace fs = std::filesystem;

namespace stdc::plugin {

    static constexpr const char *metadataSectionName = "stdc_metadata";

#ifdef _WIN32

    bool read_embedded_metadata(const fs::path &filePath, std::string *metadata,
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

#elif defined(__APPLE__)

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

    bool read_embedded_metadata(const fs::path &filePath, std::string *metadata,
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

#else

    template <class Header, class Section>
    static bool read_elf(std::ifstream &file, std::string *metadata) {
        Header header{};
        file.seekg(0);
        file.read(reinterpret_cast<char *>(&header), sizeof(header));
        if (!file || header.e_ehsize != sizeof(Header) || header.e_shentsize != sizeof(Section) ||
            header.e_shstrndx >= header.e_shnum) {
            return false;
        }
        std::vector<Section> sections(header.e_shnum);
        file.seekg(static_cast<std::streamoff>(header.e_shoff));
        file.read(reinterpret_cast<char *>(sections.data()),
                  static_cast<std::streamsize>(sections.size() * sizeof(Section)));
        if (!file) {
            return false;
        }
        const auto &namesSection = sections[header.e_shstrndx];
        std::vector<char> names(static_cast<size_t>(namesSection.sh_size));
        file.seekg(static_cast<std::streamoff>(namesSection.sh_offset));
        file.read(names.data(), static_cast<std::streamsize>(names.size()));
        if (!file) {
            return false;
        }
        for (const auto &section : sections) {
            if (section.sh_name >= names.size()) {
                continue;
            }
            if (std::strcmp(names.data() + section.sh_name, ".stdc_metadata") == 0) {
                std::string bytes(static_cast<size_t>(section.sh_size), '\0');
                file.seekg(static_cast<std::streamoff>(section.sh_offset));
                file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                if (!file) {
                    return false;
                }
                *metadata = std::move(bytes);
                return true;
            }
        }
        return false;
    }

    bool read_embedded_metadata(const fs::path &filePath, std::string *metadata,
                                std::string *errorMessage) {
        std::ifstream file(filePath, std::ios::binary);
        unsigned char identity[EI_NIDENT]{};
        file.read(reinterpret_cast<char *>(identity), sizeof(identity));
        if (!file || std::memcmp(identity, ELFMAG, SELFMAG) != 0) {
            *errorMessage = "is not an ELF library";
            return false;
        }
        bool found = false;
        if (identity[EI_CLASS] == ELFCLASS32) {
            found = read_elf<Elf32_Ehdr, Elf32_Shdr>(file, metadata);
        } else if (identity[EI_CLASS] == ELFCLASS64) {
            found = read_elf<Elf64_Ehdr, Elf64_Shdr>(file, metadata);
        }
        if (!found) {
            *errorMessage = "does not contain embedded plugin metadata";
        }
        return found;
    }

#endif

}
