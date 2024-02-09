#include <util/folder/path.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

namespace NCommandFile {
    class TCommandArgsPacker {
    public:
        TCommandArgsPacker() {}
        explicit TCommandArgsPacker(const TString& buildRoot);
        TVector<TString> Pack(const TVector<TString>& commandArgs);
    private:
        TString ConsumeCommandFileArgs(const TVector<TString>& commandArgs, size_t& pos);

        size_t Counter;
        TFsPath BuildRoot;
    };
} // namespace NCommandFile
