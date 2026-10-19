#pragma once

#include <obs.h>

namespace obsmobile {

/** Source id of "OBS Mobile · Flou", the blurred program under a full-height phone. */
constexpr const char *kBackdropSourceId = "obs_mobile_blur_backdrop";
constexpr const char *kBackdropName = "OBS Mobile · Flou";
/** Same as kCamerasScene (phone-camera.hpp), left out of the blur. */
constexpr const char *kCamerasSceneName = "OBS Mobile · Caméras";

void registerBlurBackdrop();

/** The scene on air, to blur; null when there is none. UI thread, on each scene change. */
void setBlurProgramScene(obs_source_t *scene);

} // namespace obsmobile
