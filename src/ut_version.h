// ut_version.h - the one place the mod's version is written. The start-up banner prints it (and names
// Titan Quest AE); tools\package.ps1 reads it from here for the release zip's name, and
// tools\release_check.py refuses a tree that spells the version anywhere else in the code.
#pragma once

#define UT_VERSION "1.0.1"
