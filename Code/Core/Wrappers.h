#pragma once

#include <windows.h>
#include <string>
#include <Memory/Memory.h>
#include <filesystem>
#include <format>

class File {
private:
	FILE* file;
public:
	File(std::filesystem::path path, const char* mode) { fopen_s(&file, path.string().c_str(), mode); }
	~File() { if (file) { fclose(file); } }

    bool Write(const void* data, size_t size) {
        if (!file) return false;
        return fwrite(data, 1, size, file) == size;
    }

    template <typename... Args>
    bool WriteFormat(std::format_string<Args...> fmt, Args&&... args) {
        if (!file) return false;
        try {
            std::string formatted = std::format(fmt, std::forward<Args>(args)...);
            return Write(formatted.data(), formatted.size());
        }
        catch (...) {
            return false;
        }
    }

    bool WriteString(const std::string& str) {
        return Write(str.c_str(), str.size());
    }

    bool Flush() {
        if (!file) return false;
        return fflush(file) == 0;
    }

	operator bool() const { return file != nullptr; }
	operator FILE* () { return file; }
};

inline bool IsValidUTF8(const std::string& str) {
    const unsigned char* bytes = (const unsigned char*)str.c_str();
    size_t len = str.length();

    for (size_t i = 0; i < len; i++) {
        if (bytes[i] <= 0x7F) {  // ASCII character
            continue;
        }

        // byte count of the UTF-8 character
        int charBytes;
        if ((bytes[i] & 0xE0) == 0xC0) {      // 2-byte character
            charBytes = 2;
        }
        else if ((bytes[i] & 0xF0) == 0xE0) { // 3-byte character
            charBytes = 3;
        }
        else if ((bytes[i] & 0xF8) == 0xF0) { // 4-byte character
            charBytes = 4;
        }
        else {
            return false;  // invalid UTF-8 encoding
        }

        // check continuation bytes
        for (int j = 1; j < charBytes; j++) {
            if (i + j >= len) return false;
            if ((bytes[i + j] & 0xC0) != 0x80) return false;
        }

        i += charBytes - 1;
    }
    return true;
}