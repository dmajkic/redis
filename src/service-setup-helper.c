/* Redis Windows Setup Helper functions.
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

// TODO On Windows 7+ or Windows 2008 R2+ use NetAddServiceAccount,
//      NetRemoveServiceAccount and NetIsServiceAccount.
//      NB When we do this, we MUST also stop generating a service password
//         in the setup (managed service accounts do not have a password).

#define _WIN32_WINNT 0x0501
#define WIN32_LEAN_AND_MEAN
#define STATUS_SUCCESS 0
#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <lm.h>
#include <ntsecapi.h>
#include <wchar.h>


static int addAccountRight(LPWSTR accountName, LPWSTR rightName);
static int addRemoveAccountRight(LPWSTR accountName, LPWSTR rightName, BOOL add);


/* Creates a new service account (with "log on as a service" user right).
 *
 * You can check the user rights with the "Local Security Policy" application.
 * Under the "Local Policies" / "User Rights Assignment" node see which
 * accounts are listed in the "Log on as a service" policy line.
 *
 * See How To Use NetUserAdd
 *     at http://support.microsoft.com/kb/140165
 * See How To Manage User Privileges Programmatically in Windows NT
 *     at http://support.microsoft.com/kb/132958
 */
__declspec(dllexport) int __stdcall CreateServiceAccount(LPWSTR accountName, LPWSTR password, LPWSTR comment)
{
    USER_INFO_1 userInfo;

    ZeroMemory(&userInfo, sizeof(userInfo));
    userInfo.usri1_name = accountName;
    userInfo.usri1_password = password;
    userInfo.usri1_priv = USER_PRIV_USER;
    userInfo.usri1_comment = comment;
    userInfo.usri1_flags = UF_DONT_EXPIRE_PASSWD | UF_SCRIPT; // TODO UF_NOT_DELEGATED too?

    DWORD paramError;
    NET_API_STATUS status = NetUserAdd(NULL, 1, (PBYTE)&userInfo, &paramError);

    if (status) {
        return status;
    }

    return addAccountRight(accountName, SE_SERVICE_LOGON_NAME);
}


__declspec(dllexport) int __stdcall DestroyServiceAccount(LPWSTR accountName)
{
    return addRemoveAccountRight(accountName, NULL, FALSE);
}


__declspec(dllexport) int __stdcall ServiceAccountExists(LPWSTR accountName)
{
    PUSER_INFO_0 userInfo = NULL;

    NET_API_STATUS status = NetUserGetInfo(NULL, accountName, 0, (PBYTE *)&userInfo);

    NetApiBufferFree(userInfo);

    return status;
}


static int addAccountRight(LPWSTR accountName, LPWSTR rightName)
{
    return addRemoveAccountRight(accountName, rightName, TRUE);
}


static int addRemoveAccountRight(LPWSTR accountName, LPWSTR rightName, BOOL add)
{
    BYTE sidBuffer[1024];
    DWORD sidBufferSize = sizeof(sidBuffer);
    PSID sid = sidBuffer;

    TCHAR sidReferenceDomainName[512];
    DWORD sidReferenceDomainNameLength = 512;

    SID_NAME_USE sidNameUse;

    ZeroMemory(sidBuffer, sidBufferSize);

    if (!LookupAccountName(L".", accountName, sid, &sidBufferSize, sidReferenceDomainName, &sidReferenceDomainNameLength, &sidNameUse)) {
        return -2;
    }

    LSA_HANDLE lsaHandle;
    LSA_OBJECT_ATTRIBUTES objectAttributes;

    ZeroMemory(&objectAttributes, sizeof(objectAttributes));

    if (STATUS_SUCCESS != LsaOpenPolicy(NULL, &objectAttributes, POLICY_CREATE_ACCOUNT | POLICY_LOOKUP_NAMES, &lsaHandle)) {
        return -3;
    }

    if (rightName) {
        LSA_UNICODE_STRING privilegeString;
        privilegeString.Buffer = rightName;
        privilegeString.Length = wcslen(privilegeString.Buffer)*sizeof(WCHAR);
        privilegeString.MaximumLength = privilegeString.Length+sizeof(WCHAR);

        if (add) {
            if (STATUS_SUCCESS != LsaAddAccountRights(lsaHandle, sid, &privilegeString, 1)) {
                LsaClose(lsaHandle);
                return -4;
            }
        }
        else {
            if (STATUS_SUCCESS != LsaRemoveAccountRights(lsaHandle, sid, FALSE, &privilegeString, 1)) {
                LsaClose(lsaHandle);
                return -5;
            }
        }
    }
    else if (!add) {
        // remove the account altogether...

        // NB even though the documentation says LsaRemoveAccountRights will
        //    remove the account, it does not really remove it... so we
        //    remove it manually bellow.
        if (STATUS_SUCCESS != LsaRemoveAccountRights(lsaHandle, sid, TRUE, NULL, 0)) {
            LsaClose(lsaHandle);
            return -6;
        }

        if (NERR_Success != NetUserDel(NULL, accountName)) {
            LsaClose(lsaHandle);
            return -7;
        }
    }

    LsaClose(lsaHandle);

    return 0;
}

