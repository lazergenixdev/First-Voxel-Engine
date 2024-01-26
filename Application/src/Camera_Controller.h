#pragma once
#include <Fission/Core/Engine.hh>
#include <Fission/Base/Math/Vector.hpp>
#include <Fission/Core/Input/Keys.hh>
#include <glm/vec3.hpp>     // glm::vec3
#include <glm/vec4.hpp>    // glm::vec4
#include <glm/mat4x4.hpp> // glm::mat4

struct Camera_Controller {
	glm::vec3  position = glm::vec3(0.0f);
	fs::u32    move_mask = 0;
	float      field_of_view = 1.0f;
	fs::v2f32  view_rotation;
	fs::v2f32  rotation_delta;

	static constexpr float mouse_sensitivity = 0.004f;

	enum Move_Flags {
		MOVE_FORWARD = 1 << 0,
		MOVE_BACK    = 1 << 1,
		MOVE_LEFT    = 1 << 2,
		MOVE_RIGHT   = 1 << 3,
		MOVE_UP      = 1 << 4,
		MOVE_DOWN    = 1 << 5,
	};

	auto get_chunk_position   () const -> fs::v3s32;
	auto get_position         () const -> glm::vec3;
	auto get_transform        () const -> glm::mat4;
	auto get_skybox_transform () const -> glm::mat4;
	auto get_move_vectors     () const -> std::pair<glm::vec3, glm::vec3>;
	auto get_view_direction   () const -> glm::vec3;
	auto handle_event         (fs::Event const& e) -> bool;
	auto update               (float dt) -> void;
};