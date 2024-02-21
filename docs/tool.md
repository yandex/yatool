# ya tool

Запустить иструмент доставляемый ya.

`ya tool <subcommand> [options]`

## Об инструментах, доставляемых ya

Список инструментов можно получить, набрав `ya tool` без параметров. В перечень входят:
- разрабатывамые в Яндексе инструменты для внутреннего использования;
- внешние программы, которые имеют более актуальные версии, чем предоставляют операционные системы, либо поставляются с настройками, адаптированными для использования в инфраструктуре Яндекса.

## Опции
```
    --print-path        Only print path to tool, do not execute
    --print-toolchain-path
                        Print path to toolchain root
    --print-toolchain-sys-libs
                        Print pathes to toolchsin system libraries
    --platform=PLATFORM Set specific platform
    --toolchain=TOOLCHAIN
                        Specify toolchain
    --get-param=PARAM   Get specified param
    --get-resource-id   Get resource id for specific platform (the platform should be specified)
    --get-task-id       Get task id
    --ya-help           Show help
    --target-platform=TARGET_PLATFORMS
                        Target platform
    --force-update      Check tool for updates before the update interval elapses
    --force-refetch     Refetch toolchain
  Advanced options
    --tools-cache-size=TOOLS_CACHE_SIZE
                        Max tool cache size (default: 30GiB)
  Authorization options
    --key=SSH_KEYS      Path to private ssh key to exchange for OAuth token
    --token=OAUTH_TOKEN oAuth token
    --user=USERNAME     Custom user name for authorization
```

# Список инструментов ya

```
  abcd                              Run abcd cli
  afl-fuzz                          Run afl-fuzz
  ag                                Run ag (super fast source-code grep)
  allure                            Run Allure commandline
  amduprof-cli                      AMDuProfCLI is a command-line tool for AMD uProf Profiler
  amduprof-pcm                      AMDuProfPcm is a command-line tool to monitor CPU performance metrics of AMD processors
  apphost                           Run local apphost instance
  appteka                           Run local appteka instance
  arc                               Arcadia command-line client
  atop                              Advanced System & Process Monitor
  aws                               Run aws
  bigb_ab                           bigb utility
  bigb_bb_cluster_analyzer          bigb utility
  bigb_bsyeti_experiments           bigb utility
  bigb_hitlog_analyser              bigb utility
  bigb_request                      utility to get user info
  bigb_yt_profiles_dumper           bigb utility
  bigrt_cli                         bigrt utility
  black                             Python styler, Python 3 only
  black_py2                         Python styler with Python 2 support
  blkiomon                          monitor block device I/O based o blktrace data
  blkparse                          produce formatted output of event streams of block devices from blktrace utility
  blktrace                          generate traces of the i/o traffic on block devices
  bpftool                           tool for inspection and simple manipulation of eBPF programs and maps
  bpftrace                          High-level tracing language for Linux eBPF
  btt                               analyse block i/o traces produces by blktrace
  buf                               Protobuf lint and breaking change detector
  c++                               Run C++ compiler
  c++filt                           Run c++filt
  caesar_lookup_profile             caesar utility
  caesar_profile_size_analyzer      caesar utility
  caesar_yt_sync                    caesar utility
  cc                                Run C compiler
  clang-apply-replacements          Run clang-apply-replacements companion for clang-tidy
  clang-format                      Run Clang-Format source code formatter
  clang-rename                      Run Clang-Rename refactoring tool
  clang-tidy                        Run Clang-Tidy source code linter
  cling                             Run cling
  cmake                             Run cmake
  coronerctl                        yandex-coroner cli, https://a.yandex-team.ru/arc/trunk/arcadia/infra/rsm/coroner/README.md
  crypta                            Run Crypta client
  cs                                Fast CodeSearch CLI tool
  ctags                             Run ctags
  cuda-check                        Run cuda-check
  cue                               Validate and define text-based and dynamic configuration
  dctl                              Run Yandex.Deploy CLI
  dlv                               Debugger for the Go programming language
  dns-client                        Run dns-client(tools for change RRs)
  dns-storage                       Run DNS Storage client
  eductl                            eductl is a toolchain of Yandex Education
  emacs                             Run emacs
  exp_stats                         bsyeti tool
  fio                               flexible I/O tester
  foremost                          Foremost is a Linux program to recover files based on their headers
  gcov                              Run gcov
  gdb                               Run gdb
  gdbnew                            Run gdb for Ubuntu 16.04 or later
  gdbserver                         Run gdbserver
  gdbservernew                      Run gdbserver for Ubuntu 16.04 or later
  go                                Run go tool (1.18.4)
  godoc                             Arcadia version of godoc
  gofmt                             Run gofmt tool (1.18.4)
  gpt_heap                          Google performance tools: heap checker
  gpt_perf                          Google performance tools: performance checker
  grpc_cli                          GRPC command-line tool
  grut                              Run GrUT CLI
  horadric                          Run horadric generator
  iceflame                          Performance data collection and analysis
  infractl                          Run InfraCtl CLI
  iowatcher                         Create visualizations from blktrace results
  iperf                             network load tester
  jar                               Run jar
  jar15                             Run jar from jdk15
  jar17                             Run jar from jdk17
  java                              Run java
  java10                            Run java 10
  java11                            Run java 11
  java15                            Run java 15
  java17                            Run java 17
  javac                             Run javac
  javac10                           Run javac 10
  javac11                           Run javac 11
  javac15                           Run javac 15
  javac17                           Run javac 17
  jq                                Run jq
  jstyle                            Java styler
  ktlint                            Run kotlin styler
  kubectl                           Run kubectl CLI
  lama                              Analytics tool for safe reactor calculation
  lambda                            Run tplatform serverless tool
  license_analyzer                  Run devtools license analyzer
  lkvm                              kvmtool is a userland tool for creating and controlling KVM guests
  llvm-cov                          Run llvm-cov Clang utility
  llvm-profdata                     Run llvm-profdata Clang utility
  llvm-symbolizer                   Run llvm-symbolizer Clang utility
  logbroker                         Logbroker configuration utility
  logos                             run logos helpers tools
  lz4                               Compress or decompress .lz4 files
  marketsre                         Run Market SRE Cli
  metrika-core                      Run metrika-core toolkit
  mfkit                             Toolkit for maps frontend
  mockgen                           Run GoMock (go mocking framework)
  news                              Run news team tools
  ninja                             Run ninja
  nm                                Run nm
  nots                              Everyday multitool for developing TypeScript modules in Arcadia
  nvim                              Run neovim
  objcopy                           Run objcopy
  perf                              Run Perf
  pprof                             Run pprof
  pqos                              Intel(R) Resource Director Technology monitoring and control tool
  procaas                           Run procaas CLI
  puncher                           Run Puncher CLI
  qemu                              QEMU x86_64 machine emulator and virtualizer
  qemu-i386                         QEMU i386 machine emulator and virtualizer
  qemu-img                          QEMU disk image utility
  qemu-nbd                          QEMU disk network block device server
  qyp                               QYP tool
  rdtset                            Task CPU affinity and Intel(R) Resource Director Technology control tool
  releaser                          Release tool
  rem-tool                          Run REM cli client
  renderer                          Run local renderer instance
  rex                               Run REX toolkit
  rex-cli                           Run REX CLI
  rm                                ReleaseMachine cmd tool
  rsync                             Run RSync
  rtcdiag                           RTC hosts diag tool
  rtmr-deploy                       Run RTMR deploy
  run_python_udf                    run_python_udf tool
  samogonctl                        Run Samogon Controller
  sandboxctl                        Tool to run tasks in Sandbox
  sedem                             SEDEM tool - Service management tool for Maps services
  setrace                           Run SeTrace agent
  skotty                            Skotty (SSH-agent) launcher
  sre                               Run sretool
  strace                            the linux syscall tracer
  stress-ng                         stress load tester
  strip                             Run strip utility
  svn                               Subversion command-line client
  swagger                           Run go-swagger
  tasklet                           Run tasklet CLI
  taxi-python                       Taxi backend-py3 python
  tctl                              A command-line tool for Temporal users
  temporal                          A CLI to run a Temporal Server and interact with it
  tmux                              Run Tmux
  transfer-manager                  Run Transfer Manager client
  tvmknife                          Tool for debugging and testing with TVM tickets
  uc                                Run Uber Compressor
  valgrind                          Run valgrind
  vh3                               Run VH3 CLI
  vim                               Run vim
  vmexec                            VMEXEC run script inside qemu-vm
  wall-e                            Client for Wall-E
  ya_sed                            This tool simplifies replacing of some text in Arcadia
  yadi                              Arcadia version of Yadi
  yd-migrate                        Run deploy migration
  yf                                Run YF client
  yfm-docs                          YFM-extended markdown processor for Cloud deploy (v3)
  ymakeyndexer                      Run ymake ydx converter
  yndexer                           Run Yndexer
  yo                                Tool for managing vendor/ directory
  yoimports                         Go imports formatting tool
  yp                                Run low level YP client
  yp-util                           Run YP useful stuff
  yt                                Run YT client
  ytexec                            Run ytexec
  ytyndexer                         Run YtYndexer
  zcli                              Run zcli (console client for zephyr)
  zipatcher                         Apply zipatch from file or Arcanum pull request

```
