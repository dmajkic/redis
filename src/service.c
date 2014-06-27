/* Redis Windows Service.
 *
 * Copyright (c) 2011, Rui Lopes <rgl at ruilopes dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * TODO figure out why zfree does not work when we do a 64-bit compilation
 * TODO use tcmalloc
 * TODO periodically ping redis to check it health (maybe this is not really needed; time will tell!)
 */

#define REDIS_CONFIG_LINE_MAX 1024
#define _WIN32_WINNT 0x0501
#include <winsock2.h>
#include <windows.h>
#include <unistd.h>
#include "win32fixes.h"
#include "hiredis.h"
#include "sds.h"
#include "zmalloc.h"

// yes, we are using global (module) variables! they are quite
// appropriate for this little service.

static int g_logLevel;
static char* g_fileLogPath;
static char* g_binDirectoryPath;
static char* g_redisConfPath;
static char* g_redisHost;
static int g_redisPort;
static char* g_redisPassword;
static char* g_redisAuthCommandName;
static char* g_redisShutdownCommandName;
static char* g_serviceName;
static SERVICE_STATUS g_serviceStatus;
static SERVICE_STATUS_HANDLE g_serviceSatusHandle;
static HANDLE g_redisProcess;
static HANDLE g_stopEvent;

static int initialize(int argc, char** argv);
static int loadConfiguration(const char* fileName);
static void setFileLogPathFromRedisLogFilePath(const char* redisLogFilePath);
static int setBinDirectoryPath(const char* selfPath);
static void serviceMain(int argc, char** argv);
static void serviceControlHandler(DWORD request);
static int registerServiceControlHandler(void);
static void setServiceStatus(DWORD state, DWORD exitCode);

static int startRedis(void);
static int shutdownRedis(void);


#define LOG_LEVEL_DEBUG     0
#define LOG_LEVEL_INFO      1
#define LOG_LEVEL_NOTICE    2
#define LOG_LEVEL_WARN      3
#define LOG_LEVEL_ERROR     4

#define LOG fileLog
#define LOG_DEBUG(...) LOG(LOG_LEVEL_DEBUG, ## __VA_ARGS__)
#define LOG_INFO(...) LOG(LOG_LEVEL_INFO, ## __VA_ARGS__)
#define LOG_NOTICE(...) LOG(LOG_LEVEL_NOTICE, ## __VA_ARGS__)
#define LOG_WARN(...) LOG(LOG_LEVEL_WARN, ## __VA_ARGS__)
#define LOG_ERROR(...) LOG(LOG_LEVEL_ERROR, ## __VA_ARGS__)

static int fileLog(int level, const char* format, ...);


int main(int argc, char** argv) {
    if (initialize(argc, argv)) {
        return -1;
    }

    LOG_DEBUG("Begin");

    SERVICE_TABLE_ENTRY serviceTable[2];

    serviceTable[0].lpServiceName = g_serviceName;
    serviceTable[0].lpServiceProc = (LPSERVICE_MAIN_FUNCTION)serviceMain;

    serviceTable[1].lpServiceName = NULL;
    serviceTable[1].lpServiceProc = NULL;

    if (!StartServiceCtrlDispatcher(serviceTable)) {
        DWORD lastError = GetLastError();
        switch (lastError) {
            case ERROR_FAILED_SERVICE_CONTROLLER_CONNECT:
                printf("This is a Windows service, it cannot be started directly (it has to be installed).\n");
                printf("\n");
                printf("To start, install or uninstall run (as Administrator) one of the following commands:\n");
                printf("\n");
                printf("  net start %s\n", g_serviceName);
                printf("  sc create %s binPath= %s %s %s\n", g_serviceName, argv[0], g_serviceName, g_redisConfPath);
                printf("  sc delete %s\n", g_serviceName);
                return 1;

            case ERROR_INVALID_DATA:
                LOG_ERROR("Failed to StartServiceCtrlDispatcher (ERROR_INVALID_DATA)");
                return 2;

            case ERROR_SERVICE_ALREADY_RUNNING:
                LOG_ERROR("Failed to StartServiceCtrlDispatcher (ERROR_SERVICE_ALREADY_RUNNING)");
                return 2;

            default:
                LOG_ERROR("Failed to StartServiceCtrlDispatcher (unexpected LastError of %d)", lastError);
                return -1;
        }
    }

    LOG_DEBUG("End");
    return 0;
}


static int initialize(int argc, char** argv) {
    g_logLevel = LOG_LEVEL_DEBUG;
    g_fileLogPath = "redis-service.log";
    g_serviceName = argc > 1 ? argv[1] : "redis";
    g_redisConfPath = argc > 2 ? argv[2] : "conf/redis.conf";
    g_redisHost = "127.0.0.1";
    g_redisPort = 6379;
    g_redisAuthCommandName = "AUTH";
    g_redisShutdownCommandName = "SHUTDOWN";

    if (setBinDirectoryPath(argv[0]))
        return -1;

    int result = loadConfiguration(g_redisConfPath);

    // loadConfiguration might change the working directory (because we
    // want compat with how redis handles "dir" and "include" directives),
    // but we don't want that behaviour on this service, so revert to the
    // binary directory (set by setBinDirectoryPath).
    if (chdir(g_binDirectoryPath) < 0)
        return -1;

    if (result < 0)
        return result;

    if (*g_redisAuthCommandName == 0 && g_redisPassword) {
        LOG_ERROR("The AUTH command cannot be killed. Enable it in the configuration file.");
        return -1;
    }

    if (*g_redisShutdownCommandName == 0) {
        LOG_ERROR("The SHUTDOWN command cannot be killed. Enable it in the configuration file.");
        return -1;
    }

    return 0;
}


static int loadConfiguration(const char* fileName) {
    FILE *f = fopen(fileName, "r");

    if (f == NULL) {
        fprintf(stderr, "Failed to open the redis configuration file %s\n", fileName);
        return -1;
    }

    char lineBuffer[REDIS_CONFIG_LINE_MAX + 1];

    for (int lineNumber = 1; fgets(lineBuffer, REDIS_CONFIG_LINE_MAX + 1, f); ++lineNumber) {
        sds line = sdstrim(sdsnew(lineBuffer), " \t\r\n");

        if (line[0] == '#' || line[0] == '\0') {
            sdsfree(line);
            continue;
        }

        int argc;
        sds* argv = sdssplitargs(line, &argc);

        sds name = argv[0];
        sdstolower(name);

        if (!strcmp(name, "port") && argc == 2) {
            g_redisPort = atoi(argv[1]);
        }
        else if (!strcmp(name, "bind") && argc == 2) {
            // NB currently, no one is freeing g_redisHost, but this is only
            //    set at initialization time, so don't bother for now...
            g_redisHost = zstrdup(argv[1]);
        }
        else if (!strcmp(name, "dir") && argc == 2) {
            chdir(argv[1]);
        }
        else if (!strcmp(name, "loglevel") && argc == 2) {
            if      (!strcasecmp(argv[1], "debug"))     g_logLevel = LOG_LEVEL_DEBUG;
            else if (!strcasecmp(argv[1], "verbose"))   g_logLevel = LOG_LEVEL_INFO;
            else if (!strcasecmp(argv[1], "notice"))    g_logLevel = LOG_LEVEL_NOTICE;
            else                                        g_logLevel = LOG_LEVEL_WARN;
        }
        else if (!strcmp(name, "logfile") && argc == 2) {
            setFileLogPathFromRedisLogFilePath(argv[1]);
        }
        else if (!strcmp(name, "requirepass") && argc == 2) {
            // NB currently, no one is freeing g_redisPassword, but this is only
            //    set at initialization time, so don't bother for now...
            g_redisPassword = zstrdup(argv[1]);
        }
        else if (!strcmp(name, "rename-command") && argc == 3) {
            char *commandName = argv[1];
            char *newCommandName = argv[2];

            if (!stricmp(commandName, g_redisAuthCommandName)) {
                // NB currently, no one is freeing g_redisAuthCommandName, but
                //    this is only set at initialization time, so don't bother
                //    for now...
                g_redisAuthCommandName = zstrdup(newCommandName);
            }
            else if (!stricmp(commandName, g_redisShutdownCommandName)) {
                // NB currently, no one is freeing g_redisAuthCommandName, but
                //    this is only set at initialization time, so don't bother
                //    for now...
                g_redisShutdownCommandName = zstrdup(newCommandName);
            }
        }
        else if (!strcmp(name, "include") && argc == 2) {
            loadConfiguration(argv[1]);
        }

        for (int n = 0; n < argc; ++n) {
            sdsfree(argv[n]);
        }
#ifndef _WIN64
        zfree(argv); // XXX why on 64-bit it crashes here?
#endif
        sdsfree(line);
    }

    fclose(f);
    return 0;
}


static void setFileLogPathFromRedisLogFilePath(const char* redisLogFilePath) {
    char drive[_MAX_DRIVE];
    char dir[_MAX_DIR];
    char fname[_MAX_FNAME];
    char ext[_MAX_EXT];

    _splitpath(redisLogFilePath, drive, dir, fname, ext);

    sds path = sdscatprintf(sdsempty(), "%s%s%s-service%s", drive, dir, fname, ext);

    // NB currently, no one is freeing g_fileLogPath, but this is only
    //    set at initialization time, so don't bother for now...
    g_fileLogPath = zstrdup(path);

    sdsfree(path);
}


static int setBinDirectoryPath(const char* selfPath) {
    char drive[_MAX_DRIVE];
    char dir[_MAX_DIR];
    char fname[_MAX_FNAME];
    char ext[_MAX_EXT];

    _splitpath(selfPath, drive, dir, fname, ext);

    sds directory = sdscat(sdsnew(drive), dir);

    if (chdir(directory)) {
        fprintf(stderr, "Failed to change current directory to %s\n", directory);
        sdsfree(directory);
        return -1;
    }

    // NB currently, no one is freeing g_fileLogPath, but this is only
    //    set at initialization time, so don't bother for now...
    g_binDirectoryPath = zstrdup(directory);
 
    sdsfree(directory);

    return 0;
}


static void serviceMain(int argc __attribute__((unused)), char** argv __attribute__((unused))) {
    LOG_DEBUG("Begin Service");
    
    if (!w32initWinSock()) {
        LOG_ERROR("Failed to initialize sockets");
        setServiceStatus(SERVICE_STOPPED, (DWORD)-1);
        return;
    }

    g_stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    
    if (g_stopEvent == NULL) {
        LOG_ERROR("Failed to create stop event");
        setServiceStatus(SERVICE_STOPPED, (DWORD)-1);
        return;
    }

    if (registerServiceControlHandler()) {
        LOG_ERROR("Failed to register the service control handler");
        CloseHandle(g_stopEvent);
        setServiceStatus(SERVICE_STOPPED, (DWORD)-1);
        return;
    }

    setServiceStatus(SERVICE_RUNNING, 0);

    LOG_NOTICE("Starting redis (host=%s port=%d)", g_redisHost, g_redisPort);

    if (startRedis()) {
        return;
    }

    LOG_NOTICE("Started redis (host=%s port=%d PID=%d)", g_redisHost, g_redisPort, GetProcessId(g_redisProcess));

    HANDLE waitObjects[2] = { g_redisProcess, g_stopEvent };

    DWORD waitResult = WaitForMultipleObjects(2, waitObjects, FALSE, INFINITE);

    switch (waitResult) {
        case WAIT_OBJECT_0: {
                DWORD exitCode = -1;
                GetExitCodeProcess(g_redisProcess, &exitCode);
                LOG_ERROR("Redis has been shutdown (exitCode=%u); but we didn't ask it to shutdown. check if the configuration file exists and is valid.", exitCode);
                break;
            }

        case WAIT_OBJECT_0 + 1:
            // we were asked to shutdown.
            break;

        default:
            LOG_ERROR("Failed to WaitForMultipleObjects");
            break;
    }

    if (waitResult != WAIT_OBJECT_0) {
        LOG_NOTICE("Stopping redis (PID=%d)", GetProcessId(g_redisProcess));

        if (!shutdownRedis())
            waitResult = WaitForSingleObject(g_redisProcess, INFINITE);

        if (waitResult != WAIT_OBJECT_0)
            LOG_ERROR("Redis did not stop on our command. Ignoring...");
    }

    CloseHandle(g_stopEvent);
    g_stopEvent = NULL;
    
    CloseHandle(g_redisProcess);
    g_redisProcess = NULL;

    setServiceStatus(SERVICE_STOPPED, 0);

    LOG_DEBUG("End Service");
}


static void serviceControlHandler(DWORD request) {
    switch (request) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            SetEvent(g_stopEvent);
            g_serviceStatus.dwWin32ExitCode = 0;
            g_serviceStatus.dwCurrentState  = SERVICE_STOP_PENDING;
            break;
    }

    SetServiceStatus(g_serviceSatusHandle, &g_serviceStatus);
}


static int registerServiceControlHandler(void) {
    g_serviceStatus.dwServiceType       = SERVICE_WIN32;
    g_serviceStatus.dwCurrentState      = SERVICE_START_PENDING;
    g_serviceStatus.dwControlsAccepted  = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;

    g_serviceSatusHandle = RegisterServiceCtrlHandler(g_serviceName, (LPHANDLER_FUNCTION)serviceControlHandler);

    return ((SERVICE_STATUS_HANDLE)0 == g_serviceSatusHandle) ? -1 : 0;
}


static void setServiceStatus(DWORD state, DWORD exitCode) {
    g_serviceStatus.dwCurrentState = state;
    g_serviceStatus.dwWin32ExitCode = exitCode;
    SetServiceStatus(g_serviceSatusHandle, &g_serviceStatus);
}


static redisContext* connectRedis(void) {
    redisContext* redisContext = redisConnect(g_redisHost, g_redisPort);

    if (redisContext->err) {
        LOG_ERROR("Failed to connect Redis at %s:%d: %s", g_redisHost, g_redisPort, redisContext->errstr);
        redisFree(redisContext);
        return NULL;
    }

    if (!g_redisPassword)
        return redisContext;

    redisReply *reply = redisCommand(redisContext, "%s %s", g_redisAuthCommandName, g_redisPassword);

    if (strcmp(reply->str, "OK")) {
        LOG_ERROR("Failed to authenticate Redis at %s:%d: %s", g_redisHost, g_redisPort, reply->str);
        freeReplyObject(reply);
        redisFree(redisContext);
        return NULL;
    }

    freeReplyObject(reply);
    return redisContext;
}


static int startRedis(void) {
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    ZeroMemory(&pi, sizeof(pi));

    g_redisProcess = NULL;

    sds commandLine = sdscatprintf(sdsempty(), "%s %s", g_serviceName, g_redisConfPath);

    LOG_INFO("Starting Redis with command line: %s", commandLine);

    if (!CreateProcess(
        "redis-server.exe",
        commandLine,
        NULL,
        NULL,
        FALSE,
        CREATE_NO_WINDOW | DETACHED_PROCESS,
        NULL,
        NULL,
        &si,
        &pi
    )) {
        sdsfree(commandLine);
        LOG_ERROR("Failed to CreateProcess");
        return -1;
    }

    sdsfree(commandLine);

    CloseHandle(pi.hThread);

    g_redisProcess = pi.hProcess;

    return 0;
}


static int shutdownRedis(void) {
    redisContext* redisContext = connectRedis();
 
    if (redisContext == NULL) {
        LOG_ERROR("Failed to shutdown Redis at %s:%d: could not connect", g_redisHost, g_redisPort);
        return -1;
    }

    redisReply *reply = redisCommand(redisContext, g_redisShutdownCommandName);

    if (reply != NULL) {
        if (strcmp(reply->str, "OK")) {
            LOG_ERROR("Failed to shutdown Redis at %s:%d: %s", g_redisHost, g_redisPort, reply->str);
            freeReplyObject(reply);
            redisFree(redisContext);
            return -1;
        }
        freeReplyObject(reply);
    }

    redisFree(redisContext);
    return 0;
}

static int fileLog(int level, const char* format, ...) {
    if (level < g_logLevel)
        return 0;

    FILE* log = fopen(g_fileLogPath, "a+");
    if (log == NULL)
        return -1;

    time_t now = time(NULL);

    char timeBuffer[64];

    strftime(timeBuffer, sizeof(timeBuffer), "%d %b %H:%M:%S", localtime(&now));

    const char *levelChar = ".-*##";

    fprintf(log, "[%d] %s %c ", getpid(), timeBuffer, levelChar[level]);

    va_list va;
    va_start(va, format);
    vfprintf(log, format, va);
    va_end(va);

    fprintf(log, "\n");

    fclose(log);

    return 0;
}
