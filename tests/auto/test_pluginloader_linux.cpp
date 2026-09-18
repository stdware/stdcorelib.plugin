// SPDX-License-Identifier: MIT

#include <elf.h>

#include <cstring>
#include <string>
#include <vector>

#include "pluginloader_fixture.h"

namespace {

    // The smallest ELF64 image the reader accepts, laid out so that a test can lie about one
    // field at a time. There are no program headers because the reader never looks at them.
    //
    // The section name table holds the two names back to back, so the metadata name ends exactly
    // at the end of the table. That is deliberate: the reader compares a fixed fifteen bytes, and
    // a name sitting flush against the end is the case where its bound matters.
    constexpr uint64_t sectionHeadersOffset = sizeof(Elf64_Ehdr);
    constexpr uint16_t sectionCount = 3;
    constexpr uint64_t namesOffset = sectionHeadersOffset + sectionCount * sizeof(Elf64_Shdr);
    constexpr char sectionNames[] = "\0.shstrtab\0.stdc_metadata\0";
    constexpr uint32_t namesSize = sizeof(sectionNames) - 1;
    constexpr uint32_t shstrtabName = 1;
    constexpr uint32_t metadataName = shstrtabName + sizeof(".shstrtab");

    struct ElfImage {
        Elf64_Ehdr header{};
        std::vector<Elf64_Shdr> sections;
        std::string names;
        std::string payload;

        explicit ElfImage(std::string metadata) : payload(std::move(metadata)) {
            std::memcpy(header.e_ident, ELFMAG, SELFMAG);
            header.e_ident[EI_CLASS] = ELFCLASS64;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            header.e_ident[EI_DATA] = ELFDATA2LSB;
#else
            header.e_ident[EI_DATA] = ELFDATA2MSB;
#endif
            header.e_ident[EI_VERSION] = EV_CURRENT;
            header.e_type = ET_DYN;
            header.e_version = EV_CURRENT;
            header.e_ehsize = sizeof(Elf64_Ehdr);
            header.e_shentsize = sizeof(Elf64_Shdr);
            header.e_shnum = sectionCount;
            header.e_shstrndx = 1;
            header.e_shoff = sectionHeadersOffset;

            names.assign(sectionNames, namesSize);
            sections.resize(sectionCount);

            sections[1].sh_name = shstrtabName;
            sections[1].sh_type = SHT_STRTAB;
            sections[1].sh_offset = namesOffset;
            sections[1].sh_size = namesSize;

            sections[2].sh_name = metadataName;
            sections[2].sh_type = SHT_PROGBITS;
            sections[2].sh_offset = namesOffset + namesSize;
            sections[2].sh_size = payload.size();
        }

        /// Serializes the headers as they currently stand, fixing up nothing.
        std::string build() const {
            std::string bytes(namesOffset, '\0');
            std::memcpy(bytes.data(), &header, sizeof(header));
            for (size_t i = 0; i < sections.size(); ++i) {
                std::memcpy(bytes.data() + sectionHeadersOffset + i * sizeof(Elf64_Shdr),
                            &sections[i], sizeof(Elf64_Shdr));
            }
            return bytes + names + payload;
        }
    };

    // Everything the reader rejects after it has accepted the identity bytes reports the same
    // way, because it cannot tell a truncated section table from a library that simply carries no
    // metadata.
    const char *const noMetadata = "does not contain embedded plugin metadata";

}

BOOST_AUTO_TEST_SUITE(test_pluginloader_linux)

BOOST_AUTO_TEST_CASE(test_synthetic_elf_is_accepted) {
    malformed::checkAccepted(ElfImage(malformed::envelope()).build());
}

BOOST_AUTO_TEST_CASE(test_truncated_before_identity) {
    malformed::checkRejected(ElfImage(malformed::envelope()).build().substr(0, EI_NIDENT - 1),
                             "is not an ELF library");
}

BOOST_AUTO_TEST_CASE(test_bad_elf_magic) {
    ElfImage image(malformed::envelope());
    image.header.e_ident[1] = 'X';
    malformed::checkRejected(image.build(), "is not an ELF library");
}

BOOST_AUTO_TEST_CASE(test_foreign_byte_order) {
    // The reader only handles the host's byte order. The wording it picks is the same one it uses
    // for a file that is not an ELF at all.
    ElfImage image(malformed::envelope());
    image.header.e_ident[EI_DATA] =
        image.header.e_ident[EI_DATA] == ELFDATA2LSB ? ELFDATA2MSB : ELFDATA2LSB;
    malformed::checkRejected(image.build(), "is not an ELF library");
}

BOOST_AUTO_TEST_CASE(test_unknown_elf_class) {
    ElfImage image(malformed::envelope());
    image.header.e_ident[EI_CLASS] = 7;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_header_size_disagrees) {
    ElfImage image(malformed::envelope());
    image.header.e_ehsize = sizeof(Elf64_Ehdr) + 1;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_section_header_size_disagrees) {
    ElfImage image(malformed::envelope());
    image.header.e_shentsize = sizeof(Elf64_Shdr) + 1;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_no_sections) {
    ElfImage image(malformed::envelope());
    image.header.e_shnum = 0;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_name_table_index_out_of_range) {
    ElfImage image(malformed::envelope());
    image.header.e_shstrndx = sectionCount;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_section_table_offset_past_end_of_file) {
    ElfImage image(malformed::envelope());
    image.header.e_shoff = 0xFFFFFF00;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_section_count_larger_than_the_file) {
    ElfImage image(malformed::envelope());
    image.header.e_shnum = 4096;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_empty_name_table) {
    ElfImage image(malformed::envelope());
    image.sections[1].sh_size = 0;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_name_table_offset_past_end_of_file) {
    ElfImage image(malformed::envelope());
    image.sections[1].sh_offset = 0xFFFFFF00;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_section_name_past_the_end_of_the_name_table) {
    ElfImage image(malformed::envelope());
    image.sections[2].sh_name = namesSize;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_section_name_running_off_the_name_table) {
    // One byte short of the fifteen the reader compares.
    ElfImage image(malformed::envelope());
    image.sections[2].sh_name = metadataName + 1;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_without_a_metadata_section) {
    ElfImage image(malformed::envelope());
    image.sections[2].sh_name = shstrtabName;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_empty_metadata_section) {
    ElfImage image(malformed::envelope());
    image.sections[2].sh_size = 0;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_metadata_offset_past_end_of_file) {
    ElfImage image(malformed::envelope());
    image.sections[2].sh_offset = 0xFFFFFF00;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_metadata_size_runs_past_end_of_file) {
    ElfImage image(malformed::envelope());
    image.sections[2].sh_size = 0xFFFFFF00;
    malformed::checkRejected(image.build(), noMetadata);
}

BOOST_AUTO_TEST_CASE(test_metadata_padded_with_nuls_is_accepted) {
    // The linker zero pads the section, so the reader has to tolerate the padding.
    malformed::checkAccepted(ElfImage(malformed::envelope() + std::string(64, '\0')).build());
}

BOOST_AUTO_TEST_CASE(test_metadata_section_holding_garbage) {
    malformed::checkRejected(ElfImage("not json at all").build(), nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
