#pragma once
// SCM integration for Audioslave.exe --service: service table,
// ServiceMain, control handler and status reporting. All the actual work is
// done by ServiceHost; this file only translates SCM controls to host
// requests and host state to SERVICE_STATUS.

namespace audioslave
{
// Connects to the SCM and blocks until the service stops. Returns false when
// the process was not started by the SCM.
bool runWindowsService();
} // namespace audioslave
