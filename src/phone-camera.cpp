#include "phone-camera.hpp"

#include "mobile-camera-source.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <cstring>

namespace obsmobile {

namespace {

// RAII for OBS references.
struct SourceRef {
	obs_source_t *ptr;
	explicit SourceRef(obs_source_t *p) : ptr(p) {}
	~SourceRef() { obs_source_release(ptr); }
	SourceRef(const SourceRef &) = delete;
	SourceRef &operator=(const SourceRef &) = delete;
	explicit operator bool() const { return ptr != nullptr; }
};

obs_scene_t *camerasScene(bool create = true)
{
	SourceRef existing(obs_get_source_by_name(kCamerasScene));
	if (existing)
		return obs_scene_from_source(existing.ptr); // kept alive by the frontend's scene list
	if (!create)
		return nullptr;
	obs_scene_t *created = obs_scene_create(kCamerasScene);
	obs_scene_release(created); // the frontend now holds it, like any user-created scene
	return created;
}

obs_sceneitem_t *findItem(obs_scene_t *scene, const char *sourceName)
{
	return scene ? obs_scene_find_source(scene, sourceName) : nullptr;
}

} // namespace

Layout layoutFromName(const QString &name)
{
	if (name == "full")
		return Layout::Full;
	if (name == "side")
		return Layout::Side;
	return Layout::Corner;
}

QString layoutName(Layout layout)
{
	switch (layout) {
	case Layout::Full:
		return "full";
	case Layout::Side:
		return "side";
	case Layout::Corner:
		break;
	}
	return "corner";
}

PhoneCamera::PhoneCamera(const QString &phoneName, int port)
	: inputName(QString::fromUtf8(kScenePrefix) + (phoneName.trimmed().isEmpty() ? "Téléphone" : phoneName.trimmed())),
	  srtPort(port)
{
}

PhoneCamera::~PhoneCamera()
{
	// The phone left: off the air, and its mic silent, right away.
	setVisible(false);
	if (obs_source_t *s = source()) {
		obs_source_set_muted(s, true);
		// Frees the port for the next phone.
		obs_data_t *settings = obs_data_create();
		obs_data_set_int(settings, "port", 0);
		obs_source_update(s, settings);
		obs_data_release(settings);
		obs_source_release(s);
	}
}

obs_source_t *PhoneCamera::source() const
{
	return obs_get_source_by_name(inputName.toUtf8().constData());
}

obs_sceneitem_t *PhoneCamera::item() const
{
	return findItem(camerasScene(false), inputName.toUtf8().constData());
}

void PhoneCamera::ensureSource()
{
	const QByteArray name = inputName.toUtf8();
	obs_data_t *settings = obs_data_create();
	obs_data_set_int(settings, "port", srtPort);

	SourceRef existing(obs_get_source_by_name(name.constData()));
	if (existing && strcmp(obs_source_get_unversioned_id(existing.ptr), kCameraSourceId) != 0) {
		// Left by the WebSocket (secours) mode as a Media Source: ours to replace.
		obs_log(LOG_INFO, "Replacing %s by the plugin's camera source", name.constData());
		obs_source_remove(existing.ptr);
	}
	obs_scene_t *scene = camerasScene();
	if (!findItem(scene, name.constData())) {
		SourceRef current(obs_get_source_by_name(name.constData()));
		obs_source_t *src = current ? obs_source_get_ref(current.ptr)
					    : obs_source_create(kCameraSourceId, name.constData(), settings, nullptr);
		obs_sceneitem_t *added = obs_scene_add(scene, src);
		obs_sceneitem_set_visible(added, false);
		obs_source_set_monitoring_type(src, OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT);
		obs_source_set_muted(src, true);
		obs_source_release(src);
	}
	// Starts listening: a camera source never does on its own (see mobile-camera-source.cpp).
	if (SourceRef ours(obs_get_source_by_name(name.constData())); ours)
		obs_source_update(ours.ptr, settings);
	obs_data_release(settings);
}

void PhoneCamera::place()
{
	obs_sceneitem_t *it = item();
	if (!it)
		return;
	obs_video_info ovi = {};
	obs_get_video_info(&ovi);
	const float w = float(ovi.base_width ? ovi.base_width : 1920);
	const float h = float(ovi.base_height ? ovi.base_height : 1080);

	vec2 pos, bounds;
	uint32_t alignment = OBS_ALIGN_CENTER;
	switch (layout) {
	case Layout::Full:
		vec2_set(&pos, w / 2, h / 2);
		vec2_set(&bounds, w, h);
		break;
	case Layout::Side:
		vec2_set(&pos, w * 0.75f, h / 2);
		vec2_set(&bounds, w / 2, h);
		break;
	case Layout::Corner: {
		const float margin = h * 0.03f;
		const float boxH = h * 0.45f;
		vec2_set(&pos, w - margin, h - margin);
		vec2_set(&bounds, boxH * 9 / 16, boxH);
		alignment = OBS_ALIGN_RIGHT | OBS_ALIGN_BOTTOM;
		break;
	}
	}
	obs_sceneitem_defer_update_begin(it);
	obs_sceneitem_set_bounds_type(it, OBS_BOUNDS_SCALE_INNER);
	obs_sceneitem_set_bounds_alignment(it, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds(it, &bounds);
	obs_sceneitem_set_alignment(it, alignment);
	obs_sceneitem_set_pos(it, &pos);
	obs_sceneitem_defer_update_end(it);
}

bool PhoneCamera::inCurrentScene() const
{
	obs_source_t *current = obs_frontend_get_current_scene();
	if (!current)
		return false;
	const bool present = findItem(obs_scene_from_source(current), kCamerasScene) != nullptr;
	obs_source_release(current);
	return present;
}

void PhoneCamera::addToCurrentScene()
{
	if (inCurrentScene())
		return;
	obs_source_t *current = obs_frontend_get_current_scene();
	if (!current)
		return;
	obs_scene_t *ours = camerasScene();
	obs_scene_add(obs_scene_from_source(current), obs_scene_get_source(ours));
	obs_source_release(current);
}

void PhoneCamera::start(Layout newLayout)
{
	layout = newLayout;
	ensureSource();
	place();
	addToCurrentScene();
	wantOnAir = true;
}

void PhoneCamera::stop()
{
	wantOnAir = false;
	setVisible(false);
}

void PhoneCamera::setLayout(Layout newLayout)
{
	layout = newLayout;
	place();
}

void PhoneCamera::setVisible(bool visible)
{
	obs_sceneitem_t *it = item();
	if (it && obs_sceneitem_visible(it) != visible)
		obs_sceneitem_set_visible(it, visible);
	if (obs_source_t *s = source()) {
		// The phone's mic follows its camera; the PC's mic is never touched.
		if (obs_source_muted(s) == visible)
			obs_source_set_muted(s, !visible);
		obs_source_release(s);
	}
}

QJsonObject PhoneCamera::tick()
{
	obs_source_t *s = source();
	const bool receiving = cameraIsReceiving(s);
	obs_source_release(s);
	// Shown only while pictures really arrive: never a frozen or black frame on air.
	setVisible(wantOnAir && receiving);
	obs_sceneitem_t *it = item();
	return {{"receiving", receiving},
		{"onAir", it && obs_sceneitem_visible(it)},
		{"inScene", inCurrentScene()},
		{"layout", layoutName(layout)}};
}

} // namespace obsmobile
