#include <devtools/ymake/entrypoint.h>
#include <devtools/ymake/run_main.h>

int main(int argc, char** argv) {
    return NYMake::RunWithResponseFiles(argc, argv, YMakeMain);
}
