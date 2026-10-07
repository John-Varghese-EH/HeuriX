#include <windows.h>
#include <tchar.h>
#include <iostream>
#include "heurix/heuristic_engine.hpp"
#include "heurix/grpc_server.hpp"

// Windows Service boilerplate

SERVICE_STATUS        g_ServiceStatus = {0};
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE                g_ServiceStopEvent = INVALID_HANDLE_VALUE;

VOID WINAPI ServiceMain (DWORD argc, LPTSTR *argv);
VOID WINAPI ServiceCtrlHandler (DWORD);
DWORD WINAPI ServiceWorkerThread (LPVOID lpParam);

#define SERVICE_NAME  _T("HeuriXEngine")

int _tmain (int argc, TCHAR *argv[]) {
    SERVICE_TABLE_ENTRY ServiceTable[] = {
        {SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION) ServiceMain},
        {NULL, NULL}
    };
    
    // In a real application we would check if we are launched by the SCM
    // or run interactively in the console. For now, assume SCM.
    if (StartServiceCtrlDispatcher(ServiceTable) == FALSE) {
        return GetLastError();
    }
    
    return 0;
}

VOID WINAPI ServiceMain (DWORD argc, LPTSTR *argv) {
    g_StatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);

    if (g_StatusHandle == NULL) {
        return;
    }

    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwServiceSpecificExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;

    SetServiceStatus (g_StatusHandle, &g_ServiceStatus);

    g_ServiceStopEvent = CreateEvent (NULL, TRUE, FALSE, NULL);
    if (g_ServiceStopEvent == NULL) {
        g_ServiceStatus.dwControlsAccepted = 0;
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        g_ServiceStatus.dwWin32ExitCode = GetLastError();
        g_ServiceStatus.dwCheckPoint = 1;

        SetServiceStatus (g_StatusHandle, &g_ServiceStatus);
        return;
    }

    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;

    SetServiceStatus (g_StatusHandle, &g_ServiceStatus);

    HANDLE hThread = CreateThread (NULL, 0, ServiceWorkerThread, NULL, 0, NULL);
    WaitForSingleObject (hThread, INFINITE);

    CloseHandle (g_ServiceStopEvent);

    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 3;

    SetServiceStatus (g_StatusHandle, &g_ServiceStatus);
}

VOID WINAPI ServiceCtrlHandler (DWORD CtrlCode) {
    switch (CtrlCode) {
        case SERVICE_CONTROL_STOP :
            if (g_ServiceStatus.dwCurrentState != SERVICE_RUNNING)
                break;
            
            g_ServiceStatus.dwControlsAccepted = 0;
            g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
            g_ServiceStatus.dwWin32ExitCode = 0;
            g_ServiceStatus.dwCheckPoint = 4;

            SetServiceStatus (g_StatusHandle, &g_ServiceStatus);
            SetEvent (g_ServiceStopEvent);
            break;
        default:
            break;
    }
}

DWORD WINAPI ServiceWorkerThread (LPVOID lpParam) {
    // This is where the engine initialization goes
    // heurix::GrpcServer grpc_server("0.0.0.0:50051");
    // grpc_server.Start();

    // Block until service stop event
    while (WaitForSingleObject(g_ServiceStopEvent, 0) != WAIT_OBJECT_0) {
        Sleep(1000);
    }
    
    // grpc_server.Stop();
    return ERROR_SUCCESS;
}
