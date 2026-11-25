#include <devtools/ymake/entrypoint.h>
#include <devtools/ymake/run_main.h>

#ifdef _win_
int wmain(int argc, wchar_t** argv) {
    return NYMake::RunWithWideResponseFiles(argc, argv, YMakeMain);
}
#else
int main(int argc, char** argv) {
    return NYMake::RunWithResponseFiles(argc, argv, YMakeMain);
}
#endif
