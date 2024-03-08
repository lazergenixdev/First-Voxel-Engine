#include <Fission/Core/Engine.hh>
#include "Camera_Controller.h"
#include "outline_technique.h"
#include "World_Renderer.hpp"
#include "config.hpp"

extern void display_fatal_error(const char* title, const char* what);
extern void generateMipmaps(fs::Graphics& gfx, VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);

struct app_load_data {
	float x, y, z;
	float rx, ry;
	float fov;

	static constexpr const char* filename = "app_data.bin";

	static void load(Camera_Controller& cam) {
		app_load_data data;
		if (auto f = std::ifstream(filename, std::ios::binary)) {
			f.read((char*)&data, sizeof(data));
			cam.field_of_view = data.fov;
			cam.position.x = data.x;
			cam.position.y = data.y;
			cam.position.z = data.z;
			cam.view_rotation.x = data.rx;
			cam.view_rotation.y = data.ry;
		}
	}
	static void save(Camera_Controller const& cam) {
		app_load_data data;
		data.fov = cam.field_of_view;
		data.x = cam.position.x;
		data.y = cam.position.y;
		data.z = cam.position.z;
		data.rx = cam.view_rotation.x;
		data.ry = cam.view_rotation.y;
		if (auto f = std::ofstream(filename, std::ios::binary)) {
			f.write((char*)&data, sizeof(data));
		}
	}
};

extern fs::Engine engine;

class Game_Scene : public fs::Scene
{
public:
	Game_Scene() {
	//	app_load_data::load(camera_controller);
		camera_controller.position = glm::vec3(0.0f);
		outline_technique.create(engine.graphics);
		world_renderer.create(engine.graphics, outline_technique.render_pass);
	//	world = std::make_unique<World>();
	//	skybox.create(engine.graphics, wr.render_pass);
	//	loaded_chunks.clear();
	//	world->center = {};
	//	world->generate_lod_tree();
	//	generate_meshes_from_world(*world);
		world.generate_chunks();
		world_renderer.upload_world(world);
		outline_technique.post_fx_enable = false;
	}
	virtual ~Game_Scene() override {
		world_renderer.destroy();
		outline_technique.destroy(engine.graphics);
	//	app_load_data::save(camera_controller);
	//	skybox.destroy(engine.graphics);
	}

#define toggle(X) X = !(X)
	void handle_event(fs::Event const& e) {
		if (camera_controller.handle_event(e)) return;
		switch (e.type)
		{
		default:
		break; case fs::Event_Key_Down: {
			if (e.key_down.key_id == fs::keys::F) {
				engine.window.set_mode((fs::Window_Mode)!(bool)engine.window.mode);
				engine.flags |= engine.fGraphics_Recreate_Swap_Chain;
			}
			else if (e.key_down.key_id == fs::keys::T)
				camera_controller.position = {};
			else if (e.key_down.key_id == fs::keys::L)
				toggle(world_renderer.debug_wireframe);
			else if (e.key_down.key_id == fs::keys::K)
				toggle(outline_technique.post_fx_enable);
			else if (e.key_down.key_id == fs::keys::J)
				toggle(world_renderer.debug_show_chunk_bounds);
		}
		break; case fs::Event_Key_Up: {
			if (e.key_down.key_id == fs::keys::Escape)
				engine.window.set_using_mouse_detlas(!engine.window.is_using_mouse_deltas());
		}
		}
	}

	virtual void on_update(double _dt, std::vector<fs::Event> const& events, fs::Render_Context* ctx) override {
		for (auto&& e: events)
			handle_event(e);

		using namespace fs;
		float dt = (float)_dt;

		camera_controller.update(dt);

		world_renderer.use_render_pass = false;
		outline_technique.begin(ctx);
		world_renderer.draw(*ctx, camera_controller, world, dt);
		outline_technique.end(ctx);
		
		{
			auto C = camera_controller.get_chunk_position();
			auto P = glm::ivec3(glm::round(camera_controller.position));
			engine.debug_layer.add("Position: %i,%i,%i  Chunk: %i,%i,%i", P.x, P.y, P.z, C.x, C.y, C.z);
		}
	//	engine.debug_layer.add("render wireframe: %s", FS_BTF(wireframe));
		float FOV = camera_controller.field_of_view;
		engine.debug_layer.add("FOV: %.2f (%.1f deg)", FOV, FOV * (360.0f/float(FS_TAU)));
	//	engine.debug_layer.add("number of quads: %i", total_number_of_quads);
		auto total_mib = double(total_vertex_gpu_memory)/double(1024*1024);
		auto usage = double(100 * used_vertex_gpu_memory) / double(total_vertex_gpu_memory);
		engine.debug_layer.add("GPU memory usage: %.2f%% / %.3f MiB", usage, total_mib);
	//	engine.debug_layer.add("render distance: %.0f", world->render_distance);
	}

	virtual void on_resize() override {
		world_renderer.resize_frame_buffers();
		outline_technique.resize(engine.graphics);
	}
private:
	Camera_Controller camera_controller;
	World             world;
	World_Renderer    world_renderer;
	Outline_Technique outline_technique;
};

fs::Scene* on_create_scene(fs::Scene_Key const& key) {
	return new Game_Scene;
}

fs::Defaults on_create() {
	engine.app_name = FS_str("\"something\"");
	engine.app_version = {2, 2, 0};
	engine.app_version_info = FS_str("ok");
	static auto title = std::format("VOXELS! [{}]", engine.get_version_string().str());
	return {
		.window_title = FS_str_std(title),
	};
}

