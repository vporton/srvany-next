/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Anthony Birkett
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <Windows.h>
#include <tchar.h> //For _tmain().

#define MAX_DATA_LENGTH 8192              //Max length of a registry value
#define MAX_KEY_LENGTH  MAX_PATH          //Max length of a registry path
#define SERVICE_NAME    TEXT("srvany-ng") //Name to reference this service

SERVICE_STATUS_HANDLE g_StatusHandle     = NULL;
HANDLE                g_ServiceStopEvent = INVALID_HANDLE_VALUE;
PROCESS_INFORMATION   g_Process          = { 0 };


int global_argc;
TCHAR **global_argv;

/*
 * Worker thread for the service. Keeps the service alive until stopped,
 *  or the target application exits.
 */
// DWORD WINAPI ServiceWorkerThread(LPVOID lpParam)
// {
//     UNREFERENCED_PARAMETER(lpParam);

//     while (WaitForSingleObject(g_ServiceStopEvent, 0) != WAIT_OBJECT_0)
//     {
//         //Check if the target application has exited.
//         if (WaitForSingleObject(g_Process.hProcess, 0) == WAIT_OBJECT_0)
//         {
//             //...If it has, end this thread, resulting in a service stop.
//             SetEvent(g_ServiceStopEvent);
//         }
//         Sleep(1000);
//     }

//     return ERROR_SUCCESS;
// }//end ServiceWorkerThread()


/*
 * Set the current state of the service.
 */
void ServiceSetState(DWORD acceptedControls, DWORD newState, DWORD exitCode)
{
    SERVICE_STATUS serviceStatus;
    ZeroMemory(&serviceStatus, sizeof(SERVICE_STATUS));
    serviceStatus.dwCheckPoint = 0;
    serviceStatus.dwControlsAccepted = acceptedControls;
    serviceStatus.dwCurrentState = newState;
    serviceStatus.dwServiceSpecificExitCode = 0;
    serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    serviceStatus.dwWaitHint = 0;
    serviceStatus.dwWin32ExitCode = exitCode;

    if (SetServiceStatus(g_StatusHandle, &serviceStatus) == FALSE)
    {
        OutputDebugString(TEXT("SetServiceStatus() failed\n"));
    }
}//end ServiceSetState()


/*
 * Handle service control requests, like STOP, PAUSE and CONTINUE.
 */
void WINAPI ServiceCtrlHandler(DWORD CtrlCode)
{
    switch (CtrlCode)
    {
    case SERVICE_CONTROL_STOP:
        SetEvent(g_ServiceStopEvent); //Kill the worker thread.
        TerminateProcess(g_Process.hProcess, 0); //Kill the target process.
        ServiceSetState(0, SERVICE_STOPPED, 0);
        break;

    case SERVICE_CONTROL_PAUSE:
        ServiceSetState(0, SERVICE_PAUSED, 0);
        break;

    case SERVICE_CONTROL_CONTINUE:
        ServiceSetState(0, SERVICE_RUNNING, 0);
        break;

    default:
        break;
    }
}//end ServiceCtrlHandler()


/*
 * Main entry point for the service. Acts in a similar fasion to main().
 *
 * NOTE: argv[0] will always contain the service name when invoked by the SCM.
 */
void WINAPI ServiceMain(DWORD argc, TCHAR *argv[])
{
    UNREFERENCED_PARAMETER(argc);

//Pause on start for Debug builds. Gives some time to manually attach a debugger.
#ifdef _DEBUG
    Sleep(10000);
#endif

    // TCHAR* keyPath                = (TCHAR*)calloc(MAX_KEY_LENGTH     , sizeof(TCHAR));
    // TCHAR* applicationString      = (TCHAR*)calloc(MAX_DATA_LENGTH    , sizeof(TCHAR));
    TCHAR* applicationDirectory   = (TCHAR*)calloc(MAX_DATA_LENGTH    , sizeof(TCHAR));
    TCHAR* applicationParameters  = (TCHAR*)calloc(MAX_DATA_LENGTH    , sizeof(TCHAR));
    TCHAR* applicationEnvironment = (TCHAR*)calloc(MAX_DATA_LENGTH    , sizeof(TCHAR));
    // TCHAR* appStringWithParams    = (TCHAR*)calloc(MAX_DATA_LENGTH * 2, sizeof(TCHAR));
    HKEY   openedKey;
    DWORD  cbData;

    if (/*keyPath == NULL || applicationString == NULL || applicationDirectory == NULL ||*/ applicationParameters == NULL || applicationEnvironment == NULL/* || appStringWithParams == NULL*/)
    {
        OutputDebugString(TEXT("calloc() failed\n"));
        ServiceSetState(0, SERVICE_STOPPED, GetLastError());
        return;
    }

    g_StatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);

    if (g_StatusHandle == NULL)
    {
        OutputDebugString(TEXT("RegisterServiceCtrlHandler() failed\n"));
        ServiceSetState(0, SERVICE_STOPPED, GetLastError());
        return;
    }

    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (g_ServiceStopEvent == NULL)
    {
        OutputDebugString(TEXT("CreateEvent() failed\n"));
        ServiceSetState(0, SERVICE_STOPPED, GetLastError());
        return;
    }

    //Get the target application environment from the Parameters key.
    cbData = MAX_DATA_LENGTH;
    if (RegQueryValueEx(openedKey, TEXT("AppEnvironment"), NULL, NULL, (LPBYTE)applicationEnvironment, &cbData) != ERROR_SUCCESS)
    {
        applicationEnvironment = GetEnvironmentStrings(); //Default to the current environment.
    }

    applicationDirectory = NULL;

    STARTUPINFO startupInfo;
    ZeroMemory(&startupInfo, sizeof(STARTUPINFO));
    startupInfo.cb = sizeof(STARTUPINFO);
    startupInfo.wShowWindow = 0;
    startupInfo.lpReserved = NULL;
    startupInfo.cbReserved2 = 0;
    startupInfo.lpReserved2 = NULL;

    // FIXME: Check for string length overflow.
    //Append parameters to the target command string.
    applicationParameters[0] = '\0';
    lstrcat(applicationParameters, _T("\""));
    lstrcat(applicationParameters, global_argv[1]);
    lstrcat(applicationParameters, _T("\""));
    for (int i = 2; i < global_argc; ++i) {
        lstrcat(applicationParameters, _T(" "));
        lstrcat(applicationParameters, global_argv[i]);
        // if (i != global_argc - 1) {
        // }
    }

    // wsprintf(appStringWithParams, TEXT("%s %s"), applicationString, applicationParameters);

    DWORD dwFlags = CREATE_NO_WINDOW;

//Need to specify this for Unicode envars, not needed for MBCS builds.
#ifdef UNICODE
    dwFlags |= CREATE_UNICODE_ENVIRONMENT;
#endif

    // SECURITY_ATTRIBUTES secAttrs = {
    //     sizeof(SECURITY_ATTRIBUTES),
    //     NULL,
    //     TRUE, // FIXME: or FALSE?
    // };
    // FIXME: It seems that `CreateProcess` does not return!
    //Try to launch the target application.
    if (CreateProcess(global_argv[1], applicationParameters, NULL, NULL, FALSE, dwFlags, applicationEnvironment, applicationDirectory, &startupInfo, &g_Process))
    {
        ServiceSetState(SERVICE_ACCEPT_STOP, SERVICE_RUNNING, 0);
        // HANDLE hThread = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);
        // if (hThread == NULL)
        // {
        //     fwrite("c", 1, 1, fp);
        //     ServiceSetState(0, SERVICE_STOPPED, GetLastError());
        //     fclose(fp);
        //     return;
        // }
        while (WaitForSingleObject(g_Process.hProcess, INFINITE) != WAIT_OBJECT_0)
        {}
        // WaitForSingleObject(hThread, INFINITE); //Wait here for a stop signal.
        // Sleep(INFINITE);
    }
    CloseHandle(g_ServiceStopEvent);
    CloseHandle(g_Process.hProcess);
    ServiceSetState(0, SERVICE_STOPPED, 0);
}//end ServiceMain()


/*
 * Main entry point for the application.
 *
 * NOTE: The SCM calls this, just like any other application.
 */
int _tmain(int argc, TCHAR *argv[])
{
    global_argc = argc;
    global_argv = argv;

    SERVICE_TABLE_ENTRY ServiceTable[] =
    {
        { SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain },
        { NULL, NULL }
    };

    if (StartServiceCtrlDispatcher(ServiceTable) == FALSE)
    {
        return GetLastError();
    }

    return 0;
}//end main()
