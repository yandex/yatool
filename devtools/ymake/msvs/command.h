#pragma once

#include "configuration.h"
#include "node.h"

#include <devtools/ymake/mkcmd.h>
#include <devtools/ymake/shell_subst.h>
#include <devtools/ymake/vars.h>

#include <util/generic/ptr.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

namespace NYMake {
    namespace NMsvs {
        class TCommand: public TGraphNode {
        private:
            TSubst2Shell CmdImage; // TODO: integrate back to TMakeCommand
            TSimpleSharedPtr<TMakeCommand> YMakeCommand;

        public:
            TCommand(TYMake& yMake, TNodeId id, TNodeId modId)
                : TGraphNode(yMake, id)
                , YMakeCommand(new TMakeCommand(yMake, &yMake.Conf.CommandConf)) // XXX: never do this again
            {
                YMakeCommand->CmdInfo.MkCmdAcceptor = &CmdImage;
                YMakeCommand->GetFromGraph(id, modId, ECF_Make);
            }

            inline TYVar& Inputs() const {
                return MakeCommand().Vars["INPUT"];
            }

            inline TYVar& Outputs() const {
                return MakeCommand().Vars["OUTPUT"];
            }

            inline TYVar& Tools() const {
                return MakeCommand().Vars["TOOLS"];
            }

            inline TMakeCommand& MakeCommand() const {
                return *YMakeCommand;
            }

            TString SourceInput() const;
            TString CommandText(EConf conf) const;
        };

        using TCommands = TVector<TCommand>;
    }
}
