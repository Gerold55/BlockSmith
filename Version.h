// Version.h - which BlockSmith this is: the version number, and the git commit it was built from.
// The commit comes from generated/BuildInfo.h, written by cmake/BuildInfo.cmake on each build.
// Builds without it (another build system, no git) say "unknown".
//
// (net::kVersionName in Net.h is separate: it's the network protocol's name, which servers and
// games compare to see whether they can play together.)
#pragma once

// (a build can set it instead: -DBLOCKSMITH_VERSION=\"5.18.0\")
#ifndef BLOCKSMITH_VERSION
#  define BLOCKSMITH_VERSION "5.17.0-dev"
#endif

#if defined(__has_include)
#  if __has_include("BuildInfo.h")
#    include "BuildInfo.h"
#  endif
#endif
#ifndef BLOCKSMITH_COMMIT
#  define BLOCKSMITH_COMMIT "unknown"
#endif

// "BlockSmith 5.17.0-dev (commit 1a2b3c4d5e)"
#define BLOCKSMITH_VERSION_FULL "BlockSmith " BLOCKSMITH_VERSION " (commit " BLOCKSMITH_COMMIT ")"
