#include "ros_topic_parser.h"

#include <devtools/ymake/diag/manager.h>

void TRosTopicIncludeParser::Parse(IContentHolder& file, TVector<TRosDep>& parsedIncludes) const {
    TStringBuf line;
    TStringBuf msgBuf;
    TStringBuf packageName;
    TStringBuf messageName;
    TStringBuf input = file.GetContent();
    while (input.ReadLine(line)) {
        if (line.AfterPrefix("type: ", msgBuf)) {
            if (!msgBuf.TrySplit('/', packageName, messageName)) {
                YConfErr(UserErr) << "Topic description is ill-formed: ros message should be described as ros_package_name/TypeName" << Endl;
                continue;
            }
            parsedIncludes.emplace_back(packageName, messageName);
            return;
        }
    }
    YConfErr(UserErr) << "Topic is ill-formed, no ros message to describe type" << Endl;
}
