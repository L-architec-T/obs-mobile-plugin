#include "blur-backdrop.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <graphics/graphics.h>
#include <graphics/matrix4.h>

#include <algorithm>
#include <cstring>
#include <mutex>

/*
 * "OBS Mobile · Flou": what is on air, blurred, full canvas. Put under a phone shown full height,
 * it turns the sides around the 9:16 picture into a blurred view of the scene below. OBS has no
 * blur of its own, hence this source.
 *
 * It draws the items of the scene on air (set from the UI thread by [setBlurProgramScene]) one
 * by one, at their place, leaving out "OBS Mobile · Caméras". Never the scene or the transition
 * themselves: this source is inside them, and drawing them again from here froze OBS (the
 * transition's lock) or broke the rest of the scene's drawing. If our scene is nested deeper, the
 * nested call to this source draws nothing, which ends the recursion.
 */

namespace obsmobile {

namespace {

// Blurring a small copy is both cheaper and wider than blurring the full frame.
constexpr uint32_t kDownscale = 8;
constexpr int kBlurPasses = 3;

thread_local bool renderingProgram = false;

std::mutex programMutex;
obs_weak_source_t *programScene = nullptr;

obs_source_t *currentProgram()
{
	std::lock_guard lock(programMutex);
	return programScene ? obs_weak_source_get_source(programScene) : nullptr;
}

struct Backdrop {
	obs_source_t *source = nullptr;
	gs_effect_t *effect = nullptr;
	gs_texrender_t *ping = nullptr;
	gs_texrender_t *pong = nullptr;
};

/** The scene's visible items, each at its place, except the app's own scene. */
void drawSceneItems(obs_source_t *sceneSource)
{
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene)
		return;
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *) {
			obs_source_t *source = obs_sceneitem_get_source(item);
			const char *name = obs_source_get_name(source);
			if (!obs_sceneitem_visible(item) || (name && strcmp(name, kCamerasSceneName) == 0))
				return true;
			matrix4 transform;
			obs_sceneitem_get_draw_transform(item, &transform);
			gs_matrix_push();
			gs_matrix_mul(&transform);
			obs_source_video_render(source);
			gs_matrix_pop();
			return true;
		},
		nullptr);
}

void canvasSize(uint32_t &width, uint32_t &height)
{
	obs_video_info ovi = {};
	if (obs_get_video_info(&ovi)) {
		width = ovi.base_width;
		height = ovi.base_height;
	} else {
		width = 1920;
		height = 1080;
	}
}

/** One blur pass of [from] into [to], along (dx, dy) in texels. */
void blurPass(Backdrop *self, gs_texrender_t *from, gs_texrender_t *to, uint32_t w, uint32_t h, float dx, float dy)
{
	gs_texture_t *tex = gs_texrender_get_texture(from);
	if (!tex)
		return;
	gs_texrender_reset(to);
	if (!gs_texrender_begin(to, w, h))
		return;
	gs_ortho(0.0f, float(w), 0.0f, float(h), -100.0f, 100.0f);
	vec2 texel;
	vec2_set(&texel, dx / float(w), dy / float(h));
	gs_effect_set_texture(gs_effect_get_param_by_name(self->effect, "image"), tex);
	gs_effect_set_vec2(gs_effect_get_param_by_name(self->effect, "texel"), &texel);
	while (gs_effect_loop(self->effect, "Draw"))
		gs_draw_sprite(tex, 0, w, h);
	gs_texrender_end(to);
}

const char *getName(void *)
{
	return "OBS Mobile · Flou";
}

void *create(obs_data_t *, obs_source_t *source)
{
	auto *self = new Backdrop;
	self->source = source;
	char *path = obs_module_file("effects/blur.effect");
	obs_enter_graphics();
	if (path)
		self->effect = gs_effect_create_from_file(path, nullptr);
	self->ping = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	self->pong = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	obs_leave_graphics();
	if (!self->effect)
		obs_log(LOG_ERROR, "Blur backdrop: cannot load %s", path ? path : "effects/blur.effect");
	bfree(path);
	return self;
}

void destroy(void *data)
{
	auto *self = static_cast<Backdrop *>(data);
	obs_enter_graphics();
	gs_effect_destroy(self->effect);
	gs_texrender_destroy(self->ping);
	gs_texrender_destroy(self->pong);
	obs_leave_graphics();
	delete self;
}

uint32_t getWidth(void *)
{
	uint32_t w, h;
	canvasSize(w, h);
	return w;
}

uint32_t getHeight(void *)
{
	uint32_t w, h;
	canvasSize(w, h);
	return h;
}

void render(void *data, gs_effect_t *)
{
	auto *self = static_cast<Backdrop *>(data);
	if (renderingProgram || !self->effect)
		return;
	obs_source_t *program = currentProgram();
	if (!program)
		return;

	uint32_t cw, ch;
	canvasSize(cw, ch);
	const uint32_t w = std::max<uint32_t>(cw / kDownscale, 2);
	const uint32_t h = std::max<uint32_t>(ch / kDownscale, 2);

	// 1. The program, small.
	renderingProgram = true;
	gs_texrender_reset(self->ping);
	if (gs_texrender_begin(self->ping, w, h)) {
		vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, float(cw), 0.0f, float(ch), -100.0f, 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		drawSceneItems(program);
		gs_blend_state_pop();
		gs_texrender_end(self->ping);
	}
	renderingProgram = false;
	obs_source_release(program);

	// 2. Blurred: horizontal then vertical, a few times.
	for (int i = 0; i < kBlurPasses; ++i) {
		blurPass(self, self->ping, self->pong, w, h, 1.0f, 0.0f);
		blurPass(self, self->pong, self->ping, w, h, 0.0f, 1.0f);
	}

	// 3. Stretched back to the canvas; the linear sampler smooths the upscale.
	gs_texture_t *tex = gs_texrender_get_texture(self->ping);
	if (!tex)
		return;
	vec2 none;
	vec2_zero(&none);
	gs_effect_set_texture(gs_effect_get_param_by_name(self->effect, "image"), tex);
	gs_effect_set_vec2(gs_effect_get_param_by_name(self->effect, "texel"), &none);
	while (gs_effect_loop(self->effect, "Draw"))
		gs_draw_sprite(tex, 0, cw, ch);
}

} // namespace

void setBlurProgramScene(obs_source_t *scene)
{
	obs_weak_source_t *weak = scene ? obs_source_get_weak_source(scene) : nullptr;
	std::lock_guard lock(programMutex);
	obs_weak_source_release(programScene);
	programScene = weak;
}

void registerBlurBackdrop()
{
	obs_source_info info = {};
	info.id = kBackdropSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	// Created by the plugin only: not offered in "Add source".
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_CAP_DISABLED;
	info.get_name = getName;
	info.create = create;
	info.destroy = destroy;
	info.get_width = getWidth;
	info.get_height = getHeight;
	info.video_render = render;
	info.icon_type = OBS_ICON_TYPE_IMAGE;
	obs_register_source(&info);
}

} // namespace obsmobile
