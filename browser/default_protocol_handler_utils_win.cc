/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/browser/default_protocol_handler_utils_win.h"

#include <windows.h>

#include <bcrypt.h>  // for CNG MD5
#include <sddl.h>    // for ConvertSidToStringSidW
#include <shobjidl.h>
#include <wincrypt.h>  // for CryptoAPI base64
#include <winternl.h>  // for NT_SUCCESS()
#include <wrl/client.h>

#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/notreached.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util_win.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/atl.h"
#include "base/win/registry.h"
#include "base/win/scoped_co_mem.h"
#include "base/win/scoped_com_initializer.h"
#include "base/win/win_util.h"
#include "chrome/install_static/install_util.h"
#include "chrome/installer/util/shell_util.h"

// Most of source code in this file comes from firefox's SetDefaultBrowser -
// https://github.com/mozilla/gecko-dev/blob/master/toolkit/mozapps/defaultagent/SetDefaultBrowser.cpp

namespace {

DWORD WordSwap(DWORD v) {
  return (v >> 15) | (v << 16);
}

std::unique_ptr<DWORD[]> CNG_MD5(const unsigned char* bytes, ULONG bytesLen) {
  constexpr ULONG MD5_BYTES = 16;
  constexpr ULONG MD5_DWORDS = MD5_BYTES / sizeof(DWORD);
  std::unique_ptr<DWORD[]> hash;

  BCRYPT_ALG_HANDLE hAlg = nullptr;
  if (NT_SUCCESS(::BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_MD5_ALGORITHM,
                                               nullptr, 0))) {
    BCRYPT_HASH_HANDLE hHash = nullptr;
    // As of Windows 7 the hash handle will manage its own object buffer when
    // pbHashObject is nullptr and cbHashObject is 0.
    if (NT_SUCCESS(
            ::BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0))) {
      // BCryptHashData promises not to modify pbInput.
      if (NT_SUCCESS(::BCryptHashData(hHash, const_cast<unsigned char*>(bytes),
                                      bytesLen, 0))) {
        hash = std::make_unique<DWORD[]>(MD5_DWORDS);
        if (!NT_SUCCESS(::BCryptFinishHash(
                hHash, reinterpret_cast<unsigned char*>(hash.get()),
                MD5_DWORDS * sizeof(DWORD), 0))) {
          hash.reset();
        }
      }
      ::BCryptDestroyHash(hHash);
    }
    ::BCryptCloseAlgorithmProvider(hAlg, 0);
  }

  return hash;
}

// @return The input bytes encoded as base64, nullptr on failure.
std::unique_ptr<wchar_t[]> CryptoAPI_Base64Encode(const unsigned char* bytes,
                                                  DWORD bytesLen) {
  DWORD base64Len = 0;
  if (!::CryptBinaryToStringW(bytes, bytesLen,
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                              nullptr, &base64Len)) {
    return nullptr;
  }
  auto base64 = std::make_unique<wchar_t[]>(base64Len);
  if (!::CryptBinaryToStringW(bytes, bytesLen,
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                              base64.get(), &base64Len)) {
    return nullptr;
  }

  return base64;
}

std::unique_ptr<wchar_t[]> HashString(const wchar_t* input_string) {
  auto* inputBytes = reinterpret_cast<const unsigned char*>(input_string);
  int inputByteCount = (::lstrlenW(input_string) + 1) * sizeof(wchar_t);

  constexpr size_t DWORDS_PER_BLOCK = 2;
  constexpr size_t BLOCK_SIZE = sizeof(DWORD) * DWORDS_PER_BLOCK;
  // Incomplete blocks are ignored.
  int blockCount = inputByteCount / BLOCK_SIZE;

  if (blockCount == 0) {
    return nullptr;
  }

  // Compute an MD5 hash. md5[0] and md5[1] will be used as constant multipliers
  // in the scramble below.
  auto md5 = CNG_MD5(inputBytes, inputByteCount);
  if (!md5) {
    return nullptr;
  }

  // The following loop effectively computes two checksums, scrambled like a
  // hash after every DWORD is added.

  // Constant multipliers for the scramble, one set for each DWORD in a block.
  const DWORD C0s[DWORDS_PER_BLOCK][5] = {
      {md5[0] | 1, 0xCF98B111uL, 0x87085B9FuL, 0x12CEB96DuL, 0x257E1D83uL},
      {md5[1] | 1, 0xA27416F5uL, 0xD38396FFuL, 0x7C932B89uL, 0xBFA49F69uL}};
  const DWORD C1s[DWORDS_PER_BLOCK][5] = {
      {md5[0] | 1, 0xEF0569FBuL, 0x689B6B9FuL, 0x79F8A395uL, 0xC3EFEA97uL},
      {md5[1] | 1, 0xC31713DBuL, 0xDDCD1F0FuL, 0x59C3AF2DuL, 0x35BD1EC9uL}};

  // The checksums.
  DWORD h0 = 0;
  DWORD h1 = 0;
  // Accumulated total of the checksum after each DWORD.
  DWORD h0Acc = 0;
  DWORD h1Acc = 0;

  for (int i = 0; i < blockCount; ++i) {
    for (size_t j = 0; j < DWORDS_PER_BLOCK; ++j) {
      const DWORD* C0 = C0s[j];
      const DWORD* C1 = C1s[j];

      DWORD input;
      memcpy(&input, &inputBytes[(i * DWORDS_PER_BLOCK + j) * sizeof(DWORD)],
             sizeof(DWORD));

      h0 += input;
      // Scramble 0
      h0 *= C0[0];
      h0 = WordSwap(h0) * C0[1];
      h0 = WordSwap(h0) * C0[2];
      h0 = WordSwap(h0) * C0[3];
      h0 = WordSwap(h0) * C0[4];
      h0Acc += h0;

      h1 += input;
      // Scramble 1
      h1 = WordSwap(h1) * C1[1] + h1 * C1[0];
      h1 = (h1 >> 16) * C1[2] + h1 * C1[3];
      h1 = WordSwap(h1) * C1[4] + h1;
      h1Acc += h1;
    }
  }

  DWORD hash[2] = {h0 ^ h1, h0Acc ^ h1Acc};

  return CryptoAPI_Base64Encode(reinterpret_cast<const unsigned char*>(hash),
                                sizeof(hash));
}

std::unique_ptr<wchar_t[]> FormatUserChoiceString(base::WStringPiece ext,
                                                  base::WStringPiece sid,
                                                  base::WStringPiece prog_id,
                                                  SYSTEMTIME timestamp) {
  timestamp.wSecond = 0;
  timestamp.wMilliseconds = 0;

  FILETIME file_time = {0};
  if (!::SystemTimeToFileTime(&timestamp, &file_time)) {
    return nullptr;
  }

  // This string is built into Windows as part of the UserChoice hash algorithm.
  // It might vary across Windows SKUs (e.g. Windows 10 vs. Windows Server), or
  // across builds of the same SKU, but this is the only currently known
  // version. There isn't any known way of deriving it, so we assume this
  // constant value. If we are wrong, we will not be able to generate correct
  // UserChoice hashes.
  const wchar_t* user_experience =
      L"User Choice set via Windows User Experience "
      L"{D18B6DD5-6124-4341-9318-804003BAFA0B}";

  const wchar_t* user_choice_fmt =
      L"%s%s%s"
      L"%08lx"
      L"%08lx"
      L"%s";
  int user_choice_len = _scwprintf(user_choice_fmt, ext.data(), sid.data(),
                                   prog_id.data(), file_time.dwHighDateTime,
                                   file_time.dwLowDateTime, user_experience);
  user_choice_len += 1;  // _scwprintf does not include the terminator

  auto user_choice = std::make_unique<wchar_t[]>(user_choice_len);
  _snwprintf_s(user_choice.get(), user_choice_len, _TRUNCATE, user_choice_fmt,
               ext.data(), sid.data(), prog_id.data(), file_time.dwHighDateTime,
               file_time.dwLowDateTime, user_experience);

  ::CharLowerW(user_choice.get());

  return user_choice;
}

std::unique_ptr<wchar_t[]> GenerateUserChoiceHash(base::WStringPiece ext,
                                                  base::WStringPiece sid,
                                                  base::WStringPiece prog_id,
                                                  SYSTEMTIME timestamp) {
  auto user_choice = FormatUserChoiceString(ext, sid, prog_id, timestamp);
  if (!user_choice) {
    LOG(ERROR) << "Didn't get user choice string for generating hash.";
    return nullptr;
  }

  return HashString(user_choice.get());
}

static bool AddMillisecondsToSystemTime(SYSTEMTIME& aSystemTime,
                                        ULONGLONG aIncrementMS) {
  FILETIME fileTime;
  ULARGE_INTEGER fileTimeInt;
  if (!::SystemTimeToFileTime(&aSystemTime, &fileTime)) {
    return false;
  }
  fileTimeInt.LowPart = fileTime.dwLowDateTime;
  fileTimeInt.HighPart = fileTime.dwHighDateTime;

  // FILETIME is in units of 100ns.
  fileTimeInt.QuadPart += aIncrementMS * 1000 * 10;

  fileTime.dwLowDateTime = fileTimeInt.LowPart;
  fileTime.dwHighDateTime = fileTimeInt.HighPart;
  SYSTEMTIME tmpSystemTime;
  if (!::FileTimeToSystemTime(&fileTime, &tmpSystemTime)) {
    return false;
  }

  aSystemTime = tmpSystemTime;
  return true;
}

// Compare two SYSTEMTIMEs as FILETIME after clearing everything
// below minutes.
static bool CheckEqualMinutes(SYSTEMTIME aSystemTime1,
                              SYSTEMTIME aSystemTime2) {
  aSystemTime1.wSecond = 0;
  aSystemTime1.wMilliseconds = 0;

  aSystemTime2.wSecond = 0;
  aSystemTime2.wMilliseconds = 0;

  FILETIME fileTime1;
  FILETIME fileTime2;
  if (!::SystemTimeToFileTime(&aSystemTime1, &fileTime1) ||
      !::SystemTimeToFileTime(&aSystemTime2, &fileTime2)) {
    return false;
  }

  return (fileTime1.dwLowDateTime == fileTime2.dwLowDateTime) &&
         (fileTime1.dwHighDateTime == fileTime2.dwHighDateTime);
}

std::unique_ptr<wchar_t[]> GetAssociationKeyPath(const wchar_t* aExt) {
  const wchar_t* keyPathFmt;
  if (aExt[0] == L'.') {
    keyPathFmt =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\%s";
  } else {
    keyPathFmt =
        L"SOFTWARE\\Microsoft\\Windows\\Shell\\Associations\\"
        L"UrlAssociations\\%s";
  }

  int keyPathLen = _scwprintf(keyPathFmt, aExt);
  keyPathLen += 1;  // _scwprintf does not include the terminator

  auto keyPath = std::make_unique<wchar_t[]>(keyPathLen);
  _snwprintf_s(keyPath.get(), keyPathLen, _TRUNCATE, keyPathFmt, aExt);

  return keyPath;
}

bool SetUserChoice(base::WStringPiece ext,
                   base::WStringPiece sid,
                   base::WStringPiece prog_id) {
  SYSTEMTIME hash_timestamp;
  ::GetSystemTime(&hash_timestamp);
  auto hash = GenerateUserChoiceHash(ext, sid, prog_id, hash_timestamp);
  if (!hash) {
    return false;
  }

  // The hash changes at the end of each minute, so check that the hash should
  // be the same by the time we're done writing.
  const ULONGLONG kWriteTimingThresholdMilliseconds = 100;
  // Generating the hash could have taken some time, so start from now.
  SYSTEMTIME writeEndTimestamp;
  ::GetSystemTime(&writeEndTimestamp);
  if (!AddMillisecondsToSystemTime(writeEndTimestamp,
                                   kWriteTimingThresholdMilliseconds)) {
    return false;
  }
  if (!CheckEqualMinutes(hash_timestamp, writeEndTimestamp)) {
    LOG(ERROR) << "Hash is too close to expiration, sleeping until next hash.";
    ::Sleep(kWriteTimingThresholdMilliseconds * 2);

    // For consistency, use the current time.
    ::GetSystemTime(&hash_timestamp);
    hash = GenerateUserChoiceHash(ext, sid, prog_id, hash_timestamp);
    if (!hash) {
      return false;
    }
  }

  auto assocKeyPath = GetAssociationKeyPath(ext.data());
  if (!assocKeyPath) {
    return false;
  }

  LSTATUS ls;
  HKEY rawAssocKey;
  ls = ::RegOpenKeyExW(HKEY_CURRENT_USER, assocKeyPath.get(), 0,
                       KEY_READ | KEY_WRITE, &rawAssocKey);
  if (ls != ERROR_SUCCESS) {
    LOG(ERROR) << "Can't open reg key: " << assocKeyPath.get();
    return false;
  }

  base::win::RegKey assoc_key(rawAssocKey);

  // When Windows creates this key, it is read-only (Deny Set Value), so we need
  // to delete it first.
  ls = assoc_key.DeleteKey(L"UserChoice");
  if (ls != ERROR_FILE_NOT_FOUND && ls != ERROR_SUCCESS) {
    LOG(ERROR) << "Failed to delete UserChoice key"
               << " " << ls;
    return false;
  }

  if (assoc_key.CreateKey(L"UserChoice", KEY_READ | KEY_WRITE) !=
      ERROR_SUCCESS) {
    LOG(ERROR) << "Failed to create UserChoice key";
    return false;
  }

  if (assoc_key.WriteValue(L"ProgID", prog_id.data()) != ERROR_SUCCESS) {
    LOG(ERROR) << "Failed to write ProgID value";
    return false;
  }

  if (assoc_key.WriteValue(L"Hash", hash.get()) != ERROR_SUCCESS) {
    LOG(ERROR) << "Failed to write Hash value";
    return false;
  }

  return true;
}

bool CheckProgIDExists(base::WStringPiece prog_id) {
  HKEY key;
  if (::RegOpenKeyExW(HKEY_CLASSES_ROOT, prog_id.data(), 0, KEY_READ, &key) !=
      ERROR_SUCCESS) {
    return false;
  }
  ::RegCloseKey(key);
  return true;
}

std::wstring GetBrowserProgId() {
  base::FilePath brave_exe;
  if (!base::PathService::Get(base::FILE_EXE, &brave_exe)) {
    LOG(ERROR) << "Error getting app exe path";
    return std::wstring();
  }

  const auto suffix = ShellUtil::GetCurrentInstallationSuffix(brave_exe);
  std::wstring brave_html =
      base::StrCat({install_static::GetProgIdPrefix(), suffix});

  // ProgIds cannot be longer than 39 characters.
  // Ref: http://msdn.microsoft.com/en-us/library/aa911706.aspx.
  // Make all new registrations comply with this requirement (existing
  // registrations must be preserved).
  std::wstring new_style_suffix;
  if (ShellUtil::GetUserSpecificRegistrySuffix(&new_style_suffix) &&
      suffix == new_style_suffix && brave_html.length() > 39) {
    NOTREACHED();
    brave_html.erase(39);
  }
  return brave_html;
}

std::wstring GetProgIdForProtocol(base::WStringPiece protocol) {
  Microsoft::WRL::ComPtr<IApplicationAssociationRegistration> registration;
  HRESULT hr =
      ::CoCreateInstance(CLSID_ApplicationAssociationRegistration, nullptr,
                         CLSCTX_INPROC, IID_PPV_ARGS(&registration));
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to create IApplicationAssociationRegistration";
    return std::wstring();
  }
  base::win::ScopedCoMem<wchar_t> current_app;
  const bool is_protocol = protocol[0] != L'.';
  hr = registration->QueryCurrentDefault(
      protocol.data(), is_protocol ? AT_URLPROTOCOL : AT_FILEEXTENSION,
      AL_EFFECTIVE, &current_app);
  if (FAILED(hr)) {
    LOG(ERROR) << "Failed to query default app for protocol " << protocol;
    return std::wstring();
  }
  return current_app.get();
}

}  // namespace

bool SetDefaultProtocolHandlerFor(base::WStringPiece protocol) {
  const auto prog_id = GetBrowserProgId();
  if (!CheckProgIDExists(prog_id)) {
    LOG(ERROR) << "ProgId is not found - " << prog_id;
    return false;
  }

  std::wstring user_sid;
  if (!base::win::GetUserSidString(&user_sid)) {
    LOG(ERROR) << "Can't get user sid";
    return false;
  }

  if (!SetUserChoice(protocol, user_sid, prog_id))
    return false;

  // Verify after set.
  return GetProgIdForProtocol(protocol) == prog_id;
}

bool IsDefaultProtocolHandlerFor(base::WStringPiece protocol) {
  const auto prog_id = GetBrowserProgId();
  if (!CheckProgIDExists(prog_id)) {
    LOG(ERROR) << "ProgId is not found - " << prog_id;
    return false;
  }

  return prog_id == GetProgIdForProtocol(protocol);
}
