#include "tar.h"

#include <contrib/libs/libarchive/libarchive/archive.h>
#include <contrib/libs/libarchive/libarchive/archive_entry.h>

#include <util/generic/scope.h>
#include <util/memory/blob.h>

namespace NYa::NTar {
    namespace {
        constexpr TStringBuf MSG_CREATE = "Failed to create tar";
        constexpr TStringBuf MSG_OPEN = "Failed to open tar";
        constexpr TStringBuf MSG_EXTRACT = "Failed to extract tar";
        constexpr TStringBuf MSG_READ = "Failed to read file";
        constexpr TStringBuf MSG_WRITE = "Failed to write tar";

        la_ssize_t InputCallback(struct archive*, void* client_data, const void** buffer) {
            return reinterpret_cast<IUntarInput*>(client_data)->Read(buffer);
        }

        la_ssize_t OutputCallback(struct archive*, void* client_data, const void *buffer, size_t length) {
            reinterpret_cast<IOutputStream*>(client_data)->Write(buffer, length);
            return length;
        }

        void Check(int code, struct archive *a, TStringBuf msg) {
            if (code != ARCHIVE_OK) {
                ythrow TTarError() << msg << ": " << archive_error_string(a);
            }
        }
    }

    void Tar(IOutputStream& dest, const TFsPath& rootDir, const TVector<TFsPath>& files) {
        struct archive* a = archive_write_new();
        archive_write_add_filter_none(a);
        archive_write_set_format_gnutar(a);
        archive_write_set_bytes_in_last_block(a, 1);
        Y_DEFER {
            archive_write_free(a);
        };
        Check(archive_write_open2(a, &dest, nullptr, OutputCallback, nullptr, nullptr), a, MSG_CREATE);
        struct archive_entry* entry = nullptr;
        for(const auto &path: files) {
            struct archive *disk = archive_read_disk_new();
            Y_DEFER {
                archive_read_free(disk);
            };
            Check(archive_read_disk_open(disk, path.c_str()), disk, MSG_READ);

            entry = archive_entry_new();
            Y_DEFER {
                archive_entry_free(entry);
            };
            Check(archive_read_next_header2(disk, entry), disk, "Failed to read file");

            archive_entry_set_pathname(entry, TFsPath(archive_entry_pathname(entry)).RelativeTo(rootDir).c_str());
            archive_entry_set_mtime(entry, 0, 0);
            archive_entry_set_uid(entry, 0);
            archive_entry_set_gid(entry, 0);

            Check(archive_write_header(a, entry), a, MSG_WRITE);
            if (!archive_entry_hardlink(entry) && !archive_entry_symlink(entry)) {
                auto fileInput = TBlob::FromFile(path);
                if (fileInput.size()) {
                    auto data = fileInput.data();
                    auto size = fileInput.size();
                    while (size) {
                        auto written = archive_write_data(a, data, size);
                        if (written < 0) {
                            ythrow yexception() << MSG_WRITE << ": " << path << " " << written << archive_error_string(a);
                        }
                        size -= written;
                        data += written;
                    }
                }
            }
        }
    }

    void Untar(IUntarInput& source, const TFsPath& destPath) {
        struct archive* reader = archive_read_new();
        struct archive* writer = archive_write_disk_new();
        struct archive_entry* entry = nullptr;
        const void* buf;
        archive_read_support_filter_all(reader);
        archive_read_support_format_all(reader);
        archive_write_disk_set_options(writer, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM);
        Y_DEFER {
            archive_read_free(reader);
            archive_write_free(writer);
        };
        Check(archive_read_open(reader, &source, nullptr, InputCallback, nullptr), reader, MSG_OPEN);
        while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
            archive_entry_set_pathname(entry, (destPath / archive_entry_pathname(entry)).c_str());

            if (const char* src = archive_entry_hardlink(entry); src) {
                archive_entry_set_hardlink(entry, (destPath / src).c_str());
            }

            Check(archive_write_header(writer, entry), writer, MSG_EXTRACT);

            la_int64_t offset;
            size_t size;
            int r;
            while ((r = archive_read_data_block(reader, &buf, &size, &offset)) == ARCHIVE_OK) {
                Check(archive_write_data_block(writer, buf, size, offset), writer, MSG_EXTRACT);
            }
            if (r != ARCHIVE_EOF) {
                ythrow TTarError() << "Failed to read archive: " << archive_error_string(reader);
            }

            Check(archive_write_finish_entry(writer), writer, MSG_EXTRACT);
        }
        // Ensure we readed all from source
        // Possibly will never happen IRL but useful for testing
        const void* x;
        Y_ENSURE(source.Read(&x) == 0);
    }
}
