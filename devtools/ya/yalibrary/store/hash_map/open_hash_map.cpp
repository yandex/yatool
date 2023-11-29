#include "open_hash_map.h"

#include <util/digest/city.h>
#include <util/system/fs.h>
#include <util/folder/path.h>

TOpenHashMap::TOpenHashMap(const TString& path, ui64 size, ui64 contentSize)
    : Path(path)
    , Size(size)
    , ContentSize(contentSize)
{
    if (NFs::Exists(path)) {
        i64 realSize = GetFileLength(path);
        Y_ENSURE(realSize >= 0);

        if (static_cast<ui64>(realSize) != size) {
            NFs::Remove(path);
        }
    }

    if (!NFs::Exists(path)) {
        const TString dirname = TFsPath(path).Dirname();

        if (!NFs::Exists(dirname)) {
            Y_ENSURE(NFs::MakeDirectoryRecursive(dirname));
        }

        TFile f(Path, RdWr | CreateNew);
        f.Seek(size - 1, sSet);
        char c('\0');
        f.Write(&c, 1);
        f.Close();
    }

    Map.Reset(new TFileMap(Path, TFileMap::oRdWr));
    Map->Map(0, size);
    Y_ENSURE(Map->MappedSize() == size);
}

void TOpenHashMap::SetItem(const TString& key, const void* data) {
    const ui64 hash = CityHash64(key);
    const ui64 offset = Offset(hash);

    memcpy(Ptr() + offset, &hash, HASH_SIZE);
    memcpy(Ptr() + offset + HASH_SIZE, data, ContentSize);
}

bool TOpenHashMap::GetItem(const TString& key, void* data) const {
    const ui64 hash = CityHash64(key);
    const ui64 offset = Offset(hash);

    ui64 hash2;
    memcpy(&hash2, Ptr() + offset, HASH_SIZE);

    if (hash != hash2) {
        return false;
    }

    memcpy(data, Ptr() + offset + HASH_SIZE, ContentSize);
    return true;
}

void TOpenHashMap::DelItem(const TString& key) {
    const ui64 hash = CityHash64(key);
    const ui64 offset = Offset(hash);

    memset(Ptr() + offset, '\0', PairSize());
}

char* TOpenHashMap::Ptr() const {
    return reinterpret_cast<char*>(Map->Ptr());
}

void TOpenHashMap::Flush() {
    Map->Flush();
}

TOpenHashMapIterPtr TOpenHashMap::Iter() const {
    return new TOpenHashMapIter(Ptr(), Size, ContentSize);
}
