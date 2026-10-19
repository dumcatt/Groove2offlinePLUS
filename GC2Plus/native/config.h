// Runtime options, read from
//   /sdcard/Android/data/jp.co.taito.groovecoasterzero/files/gc2plus.ini
// e.g.
//   autoplay=1
//   fastlate=1
// The file is re-read when it changes, so options can be flipped mid-session.
#pragma once

namespace gcp {

struct Config {
  bool autoplay = false;
  bool fastlate = true;
  bool interpolate = true;  // high framerate render interpolation
  // Read once at startup (hooks and patches are installed then):
  bool enabled = true;      // gc2plus=0: install nothing, the game runs unmodified
  bool autobutton = true;   // AUTO (REPLAY) button on every difficulty
  bool allavatars = true;   // every avatar selectable (not written to the save)
};

// Current options; cheap to call every frame (stats the file at most once a second).
const Config& config();

}  // namespace gcp
