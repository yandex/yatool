#pragma once

#include <util/generic/strbuf.h>

namespace NOptions {

constexpr TStringBuf ADDINCL = "ADDINCL";
constexpr TStringBuf ALIASES = "ALIASES";
constexpr TStringBuf ALLOWED = "ALLOWED";
constexpr TStringBuf ALLOWED_IN_COMMON = "ALLOWED_IN_COMMON";
constexpr TStringBuf ALL_INS_TO_OUT = "ALL_INS_TO_OUT";
constexpr TStringBuf ARGS_PARSER = "ARGS_PARSER";
constexpr TStringBuf CMD = "CMD";
constexpr TStringBuf STRUCT_CMD = "STRUCT_CMD"; // Stuctured cmd represenation DEVTOOLS-8280
constexpr TStringBuf STRUCT_SEM = "STRUCT_SEM";
constexpr TStringBuf DEFAULT_NAME_GENERATOR = "DEFAULT_NAME_GENERATOR";
constexpr TStringBuf EPILOGUE = "EPILOGUE";
constexpr TStringBuf EXTS = "EXTS";
constexpr TStringBuf FINAL_TARGET = "FINAL_TARGET";
constexpr TStringBuf GEN_FROM_FILE = "GEN_FROM_FILE";
constexpr TStringBuf GLOBAL = "GLOBAL";
constexpr TStringBuf GLOBAL_CMD = "GLOBAL_CMD";
constexpr TStringBuf GLOBAL_EXTS = "GLOBAL_EXTS";
constexpr TStringBuf GLOBAL_SEM = "GLOBAL_SEM";
constexpr TStringBuf IGNORED = "IGNORED";
constexpr TStringBuf INCLUDE_TAG = "INCLUDE_TAG";
constexpr TStringBuf NODE_TYPE = "NODE_TYPE";
constexpr TStringBuf NO_EXPAND = "NO_EXPAND";
constexpr TStringBuf PEERDIR = "PEERDIR";
constexpr TStringBuf PEERDIR_POLICY = "PEERDIR_POLICY";
constexpr TStringBuf PEERDIRSELF = "PEERDIRSELF";
constexpr TStringBuf PROXY = "PROXY";
constexpr TStringBuf VERSION_PROXY = "VERSION_PROXY";
constexpr TStringBuf RESTRICTED = "RESTRICTED";
constexpr TStringBuf SEM = "SEM";
constexpr TStringBuf SEM_IGNORE = "SEM_IGNORE";
constexpr TStringBuf SYMLINK_POLICY = "SYMLINK_POLICY";
constexpr TStringBuf USE_INJECTED_DATA = "USE_INJECTED_DATA";
constexpr TStringBuf USE_PEERS_LATE_OUTS = "USE_PEERS_LATE_OUTS";
constexpr TStringBuf FILE_GROUP = "FILE_GROUP";

}
