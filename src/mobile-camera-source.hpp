#pragma once

#include <obs.h>

namespace obsmobile {

/** Source id of a phone camera ("OBS Mobile · <phone>"). */
constexpr const char *kCameraSourceId = "obs_mobile_camera";

void registerCameraSource();

/** True while [source] (an obs_mobile_camera) decoded a picture in the last second. */
bool cameraIsReceiving(obs_source_t *source);

} // namespace obsmobile
