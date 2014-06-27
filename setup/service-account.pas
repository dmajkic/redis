// Windows User Management access code by Rui Lopes (ruilopes.com).
//
// NB You MUST use the Unicode Inno Setup to use this unit; the
//    NetXXX functions (used by our helper dll) only accept wide
//    strings.
//
// NB setup-helper.dll is copied to the app directory because we
//    need to use it at uninstall time.

function ServiceAccountExists(name: string): integer;
external 'ServiceAccountExists@files:setup-helper.dll stdcall';

function CreateServiceAccount(name, password, comment: string): integer;
external 'CreateServiceAccount@files:setup-helper.dll stdcall';

function DestroyServiceAccount(name: string): integer;
external 'DestroyServiceAccount@{app}\setup-helper.dll stdcall uninstallonly';
