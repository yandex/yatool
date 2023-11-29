#include "command.h"

namespace NYMake {
    namespace NMsvs {
        TString TCommand::CommandText(EConf conf) const {
            TString cmd = CmdImage.PrintAsLine();
            return ResolveBuildTypeSpec(YMakeCommand->CmdInfo.SubstMacroDeeply(nullptr, cmd, YMakeCommand->Vars, false), conf);
        }

        TString TCommand::SourceInput() const {
            for (const auto& input : Inputs()) {
                if (input.MsvsSource) {
                    return input.Name;
                }
            }
            return Inputs() ? Inputs().at(0).Name : "";
        }
    }
}
