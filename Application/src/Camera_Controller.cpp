#include "Camera_Controller.h"
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp> // glm::perspective
#include <glm/ext/scalar_constants.hpp> // glm::pi
#include <glm/ext/quaternion_float.hpp>
#include "config.hpp"

extern fs::Engine engine;

auto Camera_Controller::get_chunk_position() const -> fs::v3s32 {
	return fs::v3s32::from(glm::ivec3(glm::floor(position)) >> 3);
}

auto Camera_Controller::get_position() const -> glm::vec3 {
	auto P = position - glm::vec3((glm::ivec3(glm::floor(position)) >> 3) << 3);
	P += glm::vec3(float(render_chunk_radius * 8), 0, float(render_chunk_radius * 8));
	P.y = position.y;
	return P;
}

auto Camera_Controller::get_transform() const -> glm::mat4 {
	float aspect = float(engine.graphics.sc_extent.width) / float(engine.graphics.sc_extent.height);
#if 1
	glm::mat4 Projection  = glm::perspectiveLH_ZO(field_of_view, aspect, 0.1f, 1e4f);
#else // Interesting camera stuff
	glm::mat4 Perspective  = glm::perspectiveLH_ZO(field_of_view, aspect, 0.1f, 1e4f);
	float d = 10.0f;
	glm::mat4 Orthographic = glm::orthoLH_ZO(-aspect*d, aspect*d, -d, d, 0.1f, 1e4f);

	static float t = 0.0f;
	t += 0.01f;
	glm::mat4 Projection = fs::lerp(Perspective, Orthographic, abs(sinf(t)));
#endif

//	auto P = position - glm::vec3((glm::ivec3(glm::floor(position)) >> 3) << 3);
//	P += glm::vec3(float(render_chunk_radius * 8), 0, float(render_chunk_radius * 8));
//	P.y = position.y;
	auto P = position;

	auto eye = P;
	auto center = get_view_direction() + eye;
	auto up = glm::vec3(0.0f, 1.0f, 0.0f);

	return glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f))
		* Projection
		* glm::lookAtLH(eye, center, up);
		//	* glm::translate(glm::mat4(1.0f), -0.1f * position)
	//	* glm::scale(glm::mat4(1.0f), glm::vec3(0.25f));
}

auto Camera_Controller::get_skybox_transform() const -> glm::mat4
{
	glm::mat4 Projection = glm::perspectiveLH_NO(field_of_view, 16.0f / 9.0f, 0.025f, 100.f);
	glm::vec3 center = get_view_direction();
	glm::vec3 eye = { 0.0f, 0.0f, 0.0f };
	glm::vec3 up = { 0.0f, 1.0f, 0.0f };
	return { glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f))
	* Projection
	* glm::lookAtLH(eye, center, up)
	* glm::scale(glm::mat4(1.0f), glm::vec3(50.0f, 50.0f, 50.0f))
	};
}

auto Camera_Controller::get_move_vectors() const -> std::pair<glm::vec3, glm::vec3> {
	glm::vec3 center = get_view_direction();
	glm::vec3 forward = glm::normalize(glm::vec3(center.x, 0.0f, center.z));
	glm::vec3 left = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
	return { forward, left };
}

auto Camera_Controller::get_view_direction() const -> glm::vec3 {
	glm::mat4 View = glm::mat4(1.0f);
	View *= glm::rotate(View, view_rotation.x, glm::vec3(0.0f, 1.0f, 0.0f));
	View *= glm::rotate(View, view_rotation.y, glm::vec3(-1.0f, 0.0f, 0.0f));
	return View * glm::vec4{ 0.0f, 0.0f, 1.0f, 1.0f };
}

auto Camera_Controller::handle_event(fs::Event const& e) -> bool {
	if (e.type == fs::Event_Key_Down) {
		switch (e.key_down.key_id) {
		default:break;
		case fs::keys::W:     move_mask |= MOVE_FORWARD; return true;
		case fs::keys::S:     move_mask |= MOVE_BACK;    return true;
		case fs::keys::A:     move_mask |= MOVE_LEFT;    return true;
		case fs::keys::D:     move_mask |= MOVE_RIGHT;   return true;
		case fs::keys::Space: move_mask |= MOVE_UP;      return true;
		case fs::keys::Shift: move_mask |= MOVE_DOWN;    return true;
		case fs::keys::Mouse_WheelDown: field_of_view += 0.025f; return true;
		case fs::keys::Mouse_WheelUp:   field_of_view -= 0.025f; return true;
		}
	}
	else if (e.type == fs::Event_Key_Up) {
		switch (e.key_down.key_id) {
		default:break;
		case fs::keys::W:     move_mask &= ~MOVE_FORWARD; return true;
		case fs::keys::S:     move_mask &= ~MOVE_BACK;    return true;
		case fs::keys::A:     move_mask &= ~MOVE_LEFT;    return true;
		case fs::keys::D:     move_mask &= ~MOVE_RIGHT;   return true;
		case fs::keys::Space: move_mask &= ~MOVE_UP;      return true;
		case fs::keys::Shift: move_mask &= ~MOVE_DOWN;    return true;
		}
	}
	else if (e.type == fs::Event_Mouse_Move_Relative) {
		rotation_delta += (fs::v2f32)e.mouse_move_relative.delta;
		return true;
	}
	return false;
}

auto Camera_Controller::update(float dt) -> void {
	view_rotation += rotation_delta * 0.25f * fs::v2f32(mouse_sensitivity, -mouse_sensitivity);
	view_rotation.y = std::clamp(view_rotation.y, -float(FS_PI / 2.0) + 0.001f, float(FS_PI / 2.0) - 0.001f);
	view_rotation.x = std::fmodf(view_rotation.x, float(FS_TAU));
	rotation_delta = {};

	fs::v3f32 move_speed = {};

	move_mask |= MOVE_FORWARD;

	if (move_mask & MOVE_FORWARD) move_speed.x += 1.0f;
	if (move_mask & MOVE_BACK   ) move_speed.x -= 1.0f;
	if (move_mask & MOVE_RIGHT  ) move_speed.z -= 1.0f;
	if (move_mask & MOVE_LEFT   ) move_speed.z += 1.0f;
	if (move_mask & MOVE_UP     ) move_speed.y += 1.0f;
	if (move_mask & MOVE_DOWN   ) move_speed.y -= 1.0f;

	float speed = engine.modifier_keys & fs::keys::Mod_Control ? 50.0f : 5.0f;
	auto [forward, left] = get_move_vectors();
	auto up = glm::vec3(0.0f, 1.0f, 0.0f);
	position += forward * move_speed.x * speed * dt;
	position += left * move_speed.z * speed * dt;
	position += up * move_speed.y * speed * dt;

	auto v = get_view_direction();
	engine.debug_layer.add("view dir: %.1f,%.1f,%.1f", v.x, v.y, v.z);
}
