/* Copyright 2016 Peter Goodman (peter@trailofbits.com), all rights reserved. */

#ifndef CLIENT_COVERAGE_H_
#define CLIENT_COVERAGE_H_

#include <string>

namespace granary {
namespace code {

void InitPathCoverage(void);
void BeginPathCoverage(void);
void EndPathCoverage(void);
void ExitPathCoverage(void);
void ResetPathCoverage(void);
bool CoveredNewPaths(void);
std::string PathCoverageHash(void);
void MarkCoveredInputLength(void);
size_t GetCoveredInputLength(void);
size_t GetNumCoveredPaths(void);

}  // namespace code
}  // namespace granary

#endif  // CLIENT_COVERAGE_H_
