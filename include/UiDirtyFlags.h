#pragma once

struct UiDirtyFlags {
  bool full = false;
  bool status = false;
  bool primaryContent = false;
  bool disc = false;
  bool lyrics = false;
  bool metadata = false;
  bool time = false;
  bool progress = false;
  bool playbackStatus = false;
  bool volume = false;

  bool any() const {
    return full || status || primaryContent || disc || lyrics ||
           metadata || time || progress || playbackStatus || volume;
  }

  static UiDirtyFlags fullRefresh() {
    UiDirtyFlags flags;
    flags.full = true;
    return flags;
  }
};