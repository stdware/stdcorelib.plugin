// SPDX-License-Identifier: MIT

#include "pluginloader_p.h"

#include <stdcorelib/platform/windows/stdc_windows.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>

namespace fs = std::filesystem;

namespace stdc::plugin {

#if 0
    bool PluginLoader::Impl::decodeEmbeddedText(const fs::path &filePath, std::string *text,
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
            *errorMessage = "contains unreadable plugin metadata";
            ::FreeLibrary(module);
            return false;
        }

        text->assign(static_cast<const char *>(data), size);
        ::FreeLibrary(module);
        return true;
    }
#endif

    namespace {

        constexpr char metadataSectionName[IMAGE_SIZEOF_SHORT_NAME] = ".stdcmd";

        bool valid_range(uint64_t offset, uint64_t size, uint64_t fileSize) {
            return offset <= fileSize && size <= fileSize - offset;
        }

        struct HandleDeleter {
            void operator()(void *handle) const {
                ::CloseHandle(handle);
            }
        };

        struct ViewDeleter {
            void operator()(void *view) const {
                ::UnmapViewOfFile(view);
            }
        };

        using UniqueHandle = std::unique_ptr<void, HandleDeleter>;
        using UniqueView = std::unique_ptr<void, ViewDeleter>;

        template <class T>
        bool read_object(const unsigned char *fileData, uint64_t offset, uint64_t fileSize,
                         T *out) {
            if (!valid_range(offset, sizeof(T), fileSize)) {
                return false;
            }
            std::memcpy(out, fileData + offset, sizeof(T));
            return true;
        }

    }

    bool PluginLoader::Impl::decodeEmbeddedText(const fs::path &filePath, std::string *text,
                                                std::string *errorMessage) {
        auto rawFile = ::CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (rawFile == INVALID_HANDLE_VALUE) {
            *errorMessage = "could not open the library";
            return false;
        }
        UniqueHandle file(rawFile);

        LARGE_INTEGER fileSizeValue{};
        if (!::GetFileSizeEx(file.get(), &fileSizeValue) || fileSizeValue.QuadPart <= 0 ||
            uint64_t(fileSizeValue.QuadPart) > std::numeric_limits<size_t>::max()) {
            *errorMessage = "could not read the library";
            return false;
        }
        auto fileSize = static_cast<uint64_t>(fileSizeValue.QuadPart);

        UniqueHandle mapping(
            ::CreateFileMappingW(file.get(), nullptr, PAGE_READONLY, 0, 0, nullptr));
        if (!mapping) {
            *errorMessage = "could not map the library";
            return false;
        }
        UniqueView view(::MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, 0));
        if (!view) {
            *errorMessage = "could not map the library";
            return false;
        }
        auto fileData = static_cast<const unsigned char *>(view.get());

        IMAGE_DOS_HEADER dosHeader{};
        if (!read_object(fileData, 0, fileSize, &dosHeader) ||
            dosHeader.e_magic != IMAGE_DOS_SIGNATURE || dosHeader.e_lfanew < 0) {
            *errorMessage = "is not a PE library";
            return false;
        }

        auto ntOffset = static_cast<uint64_t>(dosHeader.e_lfanew);
        DWORD signature = 0;
        IMAGE_FILE_HEADER fileHeader{};
        if (!read_object(fileData, ntOffset, fileSize, &signature) ||
            signature != IMAGE_NT_SIGNATURE ||
            !read_object(fileData, ntOffset + sizeof(signature), fileSize, &fileHeader) ||
            !(fileHeader.Characteristics & IMAGE_FILE_DLL)) {
            *errorMessage = "is not a PE library";
            return false;
        }

        auto sectionsOffset = ntOffset + sizeof(signature) + sizeof(fileHeader) +
                              uint64_t(fileHeader.SizeOfOptionalHeader);
        if (!valid_range(sectionsOffset,
                         uint64_t(fileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER),
                         fileSize)) {
            *errorMessage = "contains an invalid PE section table";
            return false;
        }

        for (uint16_t i = 0; i < fileHeader.NumberOfSections; ++i) {
            IMAGE_SECTION_HEADER section{};
            auto sectionOffset = sectionsOffset + uint64_t(i) * sizeof(section);
            if (!read_object(fileData, sectionOffset, fileSize, &section)) {
                *errorMessage = "contains an invalid PE section table";
                return false;
            }
            if (std::memcmp(section.Name, metadataSectionName, sizeof(section.Name)) != 0) {
                continue;
            }
            constexpr auto disallowed = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_WRITE;
            if (!(section.Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA) ||
                section.Characteristics & disallowed) {
                *errorMessage = "contains invalid plugin metadata";
                return false;
            }

            auto size = uint64_t(section.SizeOfRawData);
            if (section.Misc.VirtualSize != 0) {
                size = std::min(size, uint64_t(section.Misc.VirtualSize));
            }
            if (size == 0 || !valid_range(section.PointerToRawData, size, fileSize) ||
                size > std::numeric_limits<size_t>::max()) {
                *errorMessage = "contains invalid plugin metadata";
                return false;
            }

            text->assign(reinterpret_cast<const char *>(fileData + section.PointerToRawData),
                         static_cast<size_t>(size));
            return true;
        }

        *errorMessage = "does not contain embedded plugin metadata";
        return false;
    }

}
