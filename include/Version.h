#pragma once
// Single version constant (P1-7). CMake passes -DSV_VERSION="<project VERSION>";
// the fallback keeps plain `g++` builds (e.g. the regression suite) working.
#ifndef SV_VERSION
#define SV_VERSION "3.0.0"
#endif
