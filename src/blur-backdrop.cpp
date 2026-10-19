#include "blur-backdrop.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <graphics/graphics.h>

#include <algorithm>

/*
 * "OBS Mobile · Flou": what is on air, blurred, full canvas. Put under a phone shown full height,
 * it turns the sides around the 9:16 picture into a blurred view of the scene below. OBS has no
 * blur of its own, hence this source.
 *
 * It renders the program (output channel 0) itself. That program contains this source (through
 * the "OBS Mobile · Caméras" scene): the nested call draws nothing, which ends the recursion.
 */

namespace obsmobile {

namespace {

// Blurring a small copy is both cheaper and wider than blurring the full frame.
constexpr uint32_t kDownscale = 8;
constexpr int kBlurPasses = 3;

thread_local bool renderingProgram = false;

struct Backdrop {
	obs_source_t *source = nullptr;
	gs_effect_t *effect = nullptr;
	gs_texrender_t *ping = nullptr;
	gs_texrender_t *pong = nullptr;
};

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
	obs_source_t *program = obs_get_output_source(0);
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
		obs_source_video_render(program);
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
