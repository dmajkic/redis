; Redis for Windows Setup Script created by Rui Lopes (ruilopes.com).
; 
; TODO after uninstall, setup-helper.dll is left behind... figure out why its
;      not being automatically deleted.
; TODO sign the setup?
;      NB: Unizeto Certum has free certificates to open-source authors.
;      See http://www.certum.eu/certum/cert,offer_software_publisher.xml
;      See https://developer.mozilla.org/en/Signing_a_XPI

#define ServiceAccountName "RedisService"
#define ServiceName "redis"
#define AppVersion GetFileVersion(AddBackslash(SourcePath) + "..\src\redis-service.exe")
#ifdef _WIN64
#define Bits "64"
#define ArchitecturesInstallIn64BitMode "x64"
#define ArchitecturesAllowed "x64"
#else
#define Bits "32"
#define ArchitecturesInstallIn64BitMode
#define ArchitecturesAllowed "x86 x64"
#endif

[Setup]
ArchitecturesInstallIn64BitMode={#ArchitecturesInstallIn64BitMode}
ArchitecturesAllowed={#ArchitecturesAllowed}
AppID={{B882ADC5-9DA9-4729-899A-F6728C146D40}
AppName=Redis
AppVersion={#AppVersion}
;AppVerName=Redis {#AppVersion}
AppPublisher=rgl
AppPublisherURL=https://github.com/rgl/redis
AppSupportURL=https://github.com/rgl/redis
AppUpdatesURL=https://github.com/rgl/redis
DefaultDirName={pf}\Redis
DefaultGroupName=Redis
LicenseFile=COPYING.txt
OutputDir=.
OutputBaseFilename=redis-setup-{#Bits}-bit
SetupIconFile=redis.ico
Compression=lzma2/max
SolidCompression=yes
WizardImageFile=redis-setup-wizard.bmp
WizardImageBackColor=$000000
WizardImageStretch=no
WizardSmallImageFile=redis-setup-wizard-small.bmp

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Dirs]
Name: "{app}\conf"
Name: "{app}\data"
Name: "{app}\logs"

[Files]
Source: "..\src\service-setup-helper.dll"; DestDir: "{app}"; DestName: "setup-helper.dll"
Source: "SetACL.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall ignoreversion
Source: "..\src\redis-benchmark.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\src\redis-check-aof.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\src\redis-check-dump.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\src\redis-cli.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\src\redis-server.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\src\redis-service.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\redis.conf"; DestDir: "{app}\conf"; DestName: "redis-dist.conf"; BeforeInstall: BeforeInstallConf; AfterInstall: AfterInstallConf;
Source: "..\README"; DestDir: "{app}"; DestName: "README.txt"; Flags: isreadme
Source: "COPYING.txt"; DestDir: "{app}"
Source: "Redis Home.url"; DestDir: "{app}"
Source: "Redis Documentation.url"; DestDir: "{app}"
Source: "Redis Windows Port Home.url"; DestDir: "{app}"
Source: "Redis Windows Service and Setup Home.url"; DestDir: "{app}"

[Icons]
Name: "{group}\Redis Client"; Filename: "{app}\redis-cli.exe"; WorkingDir: "{app}"; IconFilename: "{app}\redis-service.exe"
Name: "{group}\Redis Home"; Filename: "{app}\Redis Home.url"
Name: "{group}\Redis Documentation"; Filename: "{app}\Redis Documentation.url"
Name: "{group}\Redis Windows Port Home"; Filename: "{app}\Redis Windows Port Home.url"
Name: "{group}\Redis Windows Service and Setup Home"; Filename: "{app}\Redis Windows Service and Setup Home.url"
Name: "{group}\Redis Read Me"; Filename: "{app}\README.txt"
Name: "{group}\Redis License"; Filename: "{app}\COPYING.txt"
Name: "{group}\Uninstall Redis"; Filename: "{uninstallexe}"

[Run]
Filename: "{tmp}\SetACL.exe"; Parameters: "-on conf -ot file -actn setprot -op ""dacl:p_nc;sacl:p_nc"" -actn ace -ace ""n:{#ServiceAccountName};p:read"""; WorkingDir: "{app}"; Flags: runhidden;
Filename: "{tmp}\SetACL.exe"; Parameters: "-on data -ot file -actn setprot -op ""dacl:p_nc;sacl:p_nc"" -actn ace -ace ""n:{#ServiceAccountName};p:full"""; WorkingDir: "{app}"; Flags: runhidden;
Filename: "{tmp}\SetACL.exe"; Parameters: "-on logs -ot file -actn setprot -op ""dacl:p_nc;sacl:p_nc"" -actn ace -ace ""n:{#ServiceAccountName};p:full"""; WorkingDir: "{app}"; Flags: runhidden;

[Code]
#include "service.pas"
#include "service-account.pas"

const
  SERVICE_ACCOUNT_NAME = '{#ServiceAccountName}';
  SERVICE_ACCOUNT_DESCRIPTION = 'Redis Server Service';
  SERVICE_NAME = '{#ServiceName}';
  SERVICE_DISPLAY_NAME = 'Redis Server';
  SERVICE_DESCRIPTION = 'Persistent key-value database';

const
  LM20_PWLEN = 14;

var
  ConfDistFilePath: string;
  ConfFilePath: string;
  ReplaceExistingConfFile: boolean;

function BoolToStr(B: boolean): string;
begin
  if B then Result := 'Yes' else Result := 'No';
end;

function ToForwardSlashes(S: string): string;
begin
  Result := S;
  StringChangeEx(Result, '\', '/', True);
end;

function GeneratePassword: string;
var
  N: integer;
begin
  for N := 1 to LM20_PWLEN do
  begin
    Result := Result + Chr(33 + Random(255 - 33));
  end;
end;

procedure UpdateReplaceExistingConfFile;
var
  ConfDistFileHash: string;
  ConfFileHash: string;
begin
  ConfFilePath := ExpandConstant('{app}\conf\redis.conf');
  ConfDistFilePath := ExpandConstant('{app}\conf\redis-dist.conf');

  if not FileExists(ConfFilePath) then
  begin
    ReplaceExistingConfFile := true;
    Exit;
  end;

  if not FileExists(ConfDistFilePath) then
  begin
    ReplaceExistingConfFile := false;
    Exit;
  end;

  ConfFileHash := GetSHA1OfFile(ConfFilePath);
  ConfDistFileHash := GetSHA1OfFile(ConfDistFilePath);
  ReplaceExistingConfFile := CompareStr(ConfFileHash, ConfDistFileHash) = 0;
end;

function InitializeSetup(): boolean;
begin
  if IsServiceRunning(SERVICE_NAME) then
  begin
    MsgBox('Please stop the ' + SERVICE_NAME + ' service before running this install', mbError, MB_OK);
    Result := false;
  end
  else
    Result := true
end;

function InitializeUninstall(): boolean;
begin
  if IsServiceRunning(SERVICE_NAME) then
  begin
    MsgBox('Please stop the ' + SERVICE_NAME + ' service before running this uninstall', mbError, MB_OK);
    Result := false;
  end
  else
    Result := true;

  UpdateReplaceExistingConfFile;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ServicePath: string;
  Password: string;
  Status: integer;
begin
  case CurStep of
    ssInstall:
      begin
        if ServiceAccountExists(SERVICE_ACCOUNT_NAME) <> 0 then
        begin
          Password := GeneratePassword;

          Status := CreateServiceAccount(SERVICE_ACCOUNT_NAME, Password, SERVICE_ACCOUNT_DESCRIPTION);

          if Status <> 0 then
          begin
            MsgBox('Failed to create service account for ' + SERVICE_ACCOUNT_NAME + ' (#' + IntToStr(Status) + ')' #13#13 'You need to create it manually.', mbError, MB_OK);
          end;
        end;

        if IsServiceInstalled(SERVICE_NAME) then
          Exit;

        ServicePath := ExpandConstant('{app}\redis-service.exe');

        if not InstallService(ServicePath, SERVICE_NAME, SERVICE_DISPLAY_NAME, SERVICE_DESCRIPTION, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ACCOUNT_NAME, Password) then
        begin
          MsgBox('Failed to install the ' + SERVICE_NAME + ' service.' #13#13 'You need to install it manually.', mbError, MB_OK)
        end
      end
  end
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Status: integer;
begin
  case CurUninstallStep of
    usUninstall:
      begin
        if ReplaceExistingConfFile then
          DeleteFile(ConfFilePath);
      end;

    usPostUninstall:
      begin
        if not RemoveService(SERVICE_NAME) then
        begin
          MsgBox('Failed to uninstall the ' + SERVICE_NAME + ' service.' #13#13 'You need to uninstall it manually.', mbError, MB_OK);
        end;

        Status := DestroyServiceAccount(SERVICE_ACCOUNT_NAME);

        if Status <> 0 then
        begin
          MsgBox('Failed to delete the service account for ' + SERVICE_ACCOUNT_NAME + ' (#' + IntToStr(Status) + ')' #13#13 'You need to delete it manually.', mbError, MB_OK);
        end;
      end
  end
end;

procedure BeforeInstallConf;
begin
  UpdateReplaceExistingConfFile;
end;

procedure AfterInstallConf;
var
  BasePath: string;
  ConfLines: TArrayOfString;
  N: integer;
  Line: string;
begin
  BasePath := RemoveBackslash(ExpandConstant('{app}'));

  if not LoadStringsFromFile(ConfDistFilePath, ConfLines) then
  begin
    MsgBox('Failed to load the ' + ConfDistFilePath + ' configuration file.' #13#13 'This program will not run correctly unless you manually edit the configuration file.', mbError, MB_OK);
    Abort;
  end;

  // NB we need to escape the backslashes in the string arguments on the redis
  //    configuration file. If we are using a file path, we can instead use
  //    forward slashes.

  for N := 0 to GetArrayLength(ConfLines)-1 do
  begin
    Line := Trim(ConfLines[N]);

    if Pos('#', Line) = 1 then
      Continue;

    if Pos('dir ', Line) = 1 then
    begin
      ConfLines[N] := ToForwardSlashes(Format('dir "%s\data"', [BasePath]));
      Continue;
    end;

    if Pos('vm-swap-file ', Line) = 1 then
    begin
      ConfLines[N] := ToForwardSlashes(Format('vm-swap-file "%s\data\redis.swap"', [BasePath]));
      Continue;
    end;

    if Pos('logfile ', Line) = 1 then
    begin
      ConfLines[N] := ToForwardSlashes(Format('logfile "%s\logs\redis.log"', [BasePath]));
      Continue;
    end;
  end;

  if not SaveStringsToFile(ConfDistFilePath, ConfLines, false) then
  begin
    MsgBox('Failed to save the ' + ConfDistFilePath + ' configuration file.' #13#13 'This program will not run correctly unless you manually edit the configuration file.', mbError, MB_OK);
    Abort;
  end;

  if ReplaceExistingConfFile then
    FileCopy(ConfDistFilePath, ConfFilePath, false);
end;
