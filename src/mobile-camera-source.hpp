#pragma once

#include <obs.h>

#include <string>

namespace obsmobile {

/** Source id of a phone camera ("OBS Mobile · <phone>"). */
constexpr const char *kCameraSourceId = "obs_mobile_camera";

void registerCameraSource();

/** Why [source] cannot receive its phone right now, in French for the phone; empty when fine. */
std::string cameraProblem(obs_source_t *source);

/** True while [source] (an obs_mobile_camera) decoded a picture in the last second. */
bool cameraIsReceiving(obs_source_t *source);

} // namespace obsmobile
