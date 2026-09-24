#pragma once
// `devices`, `diagnose` and `validate` (CLI diagnostics).

namespace audioslave::cli
{
int runDevices();
int runDiagnose (bool probeExclusive);
int runValidate();
} // namespace audioslave::cli
