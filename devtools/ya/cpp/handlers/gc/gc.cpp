#include <devtools/ya/cpp/lib/class_registry.h>
#include <devtools/ya/cpp/lib/config.h>
#include <devtools/ya/cpp/lib/ya_handler.h>

#include <util/generic/yexception.h>
#include <util/string/printf.h>
#include <util/system/env.h>
#include <util/system/file.h>
#include <util/system/getpid.h>

namespace NYa::NTool {
    struct TGcHandler: public IYaHandler {
        void Run(const TVector<TStringBuf>& args) override {
            Y_UNUSED(args);

            // Disable any logging and pass control to the python entrypoint
            // if we don't have enough space to create a 10kb file.
            const size_t fileSize = 10 * 1024;
            const TString fileName = Sprintf("gc_test.%d.blob", GetPID());
            const TFsPath filePath = GetConfig().TmpRoot() / fileName;
            try {
                TFile file(filePath, CreateNew | Transient);
                file.Seek(fileSize, sSet);
                file.Write("\0", 1);
                file.Close();
            } catch (const TFileError&) {
                SetEnv("YA_NO_LOGS", "1");
                SetEnv("YA_NO_TMP_DIR", "1");
            }
        }

        bool AllowLogging() const override {
            // Disable logging for cpp entrypoint to avoid trying to write to disk when there is no space
            return false;
        }
    };

    static TClassRegistrar<IYaHandler, TGcHandler> registration{"gc"};
} // namespace NYa::NTool
