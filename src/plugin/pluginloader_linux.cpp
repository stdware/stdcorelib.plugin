// SPDX-License-Identifier: MIT

#include "pluginloader_p.h"

#include <elf.h>

#include <cstring>
#include <fstream>
#include <limits>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace stdc::plugin {

    static constexpr char metadataSectionName[] = ".stdc_metadata";

    static bool valid_range(uint64_t offset, uint64_t size, uint64_t fileSize) {
        return offset <= fileSize && size <= fileSize - offset;
    }

    static bool readable_size(uint64_t size) {
        return size <= std::numeric_limits<size_t>::max() &&
               size <= static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max());
    }

    template <class Header, class Section>
    static bool read_elf(std::ifstream &file, uint64_t fileSize, std::string *manifest) {
        Header header{};
        file.seekg(0);
        file.read(reinterpret_cast<char *>(&header), sizeof(header));
        if (!file || header.e_ehsize != sizeof(Header) || header.e_shentsize != sizeof(Section) ||
            header.e_shnum == 0 || header.e_shstrndx >= header.e_shnum ||
            !valid_range(header.e_shoff, uint64_t(header.e_shnum) * sizeof(Section), fileSize)) {
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
        if (namesSection.sh_size == 0 ||
            !valid_range(namesSection.sh_offset, namesSection.sh_size, fileSize)) {
            return false;
        }
        for (const auto &section : sections) {
            if (section.sh_name >= namesSection.sh_size ||
                sizeof(metadataSectionName) > namesSection.sh_size - section.sh_name) {
                continue;
            }
            char name[sizeof(metadataSectionName)]{};
            file.seekg(static_cast<std::streamoff>(namesSection.sh_offset + section.sh_name));
            file.read(name, sizeof(name));
            if (!file) {
                return false;
            }
            if (std::memcmp(name, metadataSectionName, sizeof(metadataSectionName)) == 0) {
                if (section.sh_size == 0 ||
                    !valid_range(section.sh_offset, section.sh_size, fileSize) ||
                    !readable_size(section.sh_size)) {
                    return false;
                }
                std::string bytes(static_cast<size_t>(section.sh_size), '\0');
                file.seekg(static_cast<std::streamoff>(section.sh_offset));
                file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                if (!file) {
                    return false;
                }
                *manifest = std::move(bytes);
                return true;
            }
        }
        return false;
    }

    bool PluginLoader::Impl::readEmbeddedManifest(const fs::path &filePath, std::string *manifest,
                                                  std::string *errorMessage) {
        std::ifstream file(filePath, std::ios::binary);
        file.seekg(0, std::ios::end);
        auto end = file.tellg();
        if (end < 0) {
            *errorMessage = "could not read the library";
            return false;
        }
        auto fileSize = static_cast<uint64_t>(end);
        file.seekg(0);

        unsigned char identity[EI_NIDENT]{};
        file.read(reinterpret_cast<char *>(identity), sizeof(identity));
        if (!file || std::memcmp(identity, ELFMAG, SELFMAG) != 0 ||
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            identity[EI_DATA] != ELFDATA2LSB) {
#else
            identity[EI_DATA] != ELFDATA2MSB) {
#endif
            *errorMessage = "is not an ELF library";
            return false;
        }
        bool found = false;
        if (identity[EI_CLASS] == ELFCLASS32) {
            found = read_elf<Elf32_Ehdr, Elf32_Shdr>(file, fileSize, manifest);
        } else if (identity[EI_CLASS] == ELFCLASS64) {
            found = read_elf<Elf64_Ehdr, Elf64_Shdr>(file, fileSize, manifest);
        }
        if (!found) {
            *errorMessage = "does not contain an embedded plugin manifest";
        }
        return found;
    }

}
