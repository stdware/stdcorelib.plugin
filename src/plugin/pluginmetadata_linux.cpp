// SPDX-License-Identifier: MIT

#include "pluginmetadata_p.h"

#include <cstring>
#include <elf.h>
#include <fstream>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace stdc::plugin {

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

}
