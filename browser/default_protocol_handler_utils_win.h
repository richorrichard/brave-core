/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_DEFAULT_PROTOCOL_HANDLER_UTILS_WIN_H_
#define BRAVE_BROWSER_DEFAULT_PROTOCOL_HANDLER_UTILS_WIN_H_

#include <string>
#include "base/strings/string_piece.h"

bool SetDefaultProtocolHandlerFor(base::WStringPiece protocol);
bool IsDefaultProtocolHandlerFor(base::WStringPiece protocol);

#endif  // BRAVE_BROWSER_DEFAULT_PROTOCOL_HANDLER_UTILS_WIN_H_
