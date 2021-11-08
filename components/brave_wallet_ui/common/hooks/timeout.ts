// Copyright (c) 2021 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at http://mozilla.org/MPL/2.0/.

import * as React from 'react'

export default function useTimeout (
  notifyUserInteraction: () => void,
  isWalletLocked: boolean
) {
  const hasInteractedSinceInterval = React.useRef<boolean>(false)
  const onMouseMove = () => {
    if (!hasInteractedSinceInterval.current) {
      hasInteractedSinceInterval.current = true
    }
  }

  const onKeyDown = () => {
    if (!hasInteractedSinceInterval.current) {
      hasInteractedSinceInterval.current = true
    }
  }

  const onVisibilityChange = () => {
    if (document.visibilityState === 'visible' && !hasInteractedSinceInterval.current) {
      hasInteractedSinceInterval.current = true
    }
  }

  React.useEffect(() => {
    const removeEventListeners = () => {
      window.removeEventListener('mousemove', onMouseMove)
      window.removeEventListener('keydown', onKeyDown)
      document.removeEventListener('visibilitychange', onVisibilityChange)
    }

    if (!isWalletLocked) {
      window.addEventListener('mousemove', onMouseMove)
      window.addEventListener('keydown', onKeyDown)
      document.addEventListener('visibilitychange', onVisibilityChange)
    }
    return removeEventListeners
  }, [isWalletLocked, hasInteractedSinceInterval])

  React.useEffect(() => {
    if (!isWalletLocked) {
      const interval = setInterval(() => {
        if (hasInteractedSinceInterval.current) {
          hasInteractedSinceInterval.current = false
          notifyUserInteraction()
        }
      }, 50000)
      return () => clearInterval(interval)
    }
    return
  }, [isWalletLocked, hasInteractedSinceInterval])
}
