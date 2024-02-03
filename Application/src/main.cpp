#include <Fission/Core/Engine.hh>
#include <Fission/Core/Input/Keys.hh>
#include <Fission/Base/Time.hpp>
#include <Fission/Base/Math/Vector.hpp>
#include "Chunk.hpp"
#include <random>
#include <format>
#include <execution>
#include <fstream>
#include "Camera_Controller.h"
#include "Skybox.h"
#include "outline_technique.h"
#define STB_IMAGE_IMPLEMENTATION 1
#include <stb_image.h>
#define STB_PERLIN_IMPLEMENTATION 1
#include <stb_perlin.h>
#include "mesher.hpp"
#include "config.hpp"
#include "job_queue.hpp"

extern void display_fatal_error(const char* title, const char* what);
extern void generateMipmaps(fs::Graphics& gfx, VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);

struct vertex {
	fs::v3f32 position;
};

struct draw_data {
	int vertex_count = 0;
	int index_count = 0;
};

static float _amplitude = 0.0f;
static float _time = 0.0f;

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


// This is a terrible rain implementation, very not optimal
#if RAIN
struct Rain {
	struct Particle {
		fs::v3f32 position;
	};

	struct Emitter {
		Particle emit() {
			return {};
		}
	};

	VkBuffer vertex_buffer;
	VkBuffer index_buffer;
	VmaAllocation vertex_allocation;
	VmaAllocation index_allocation;

	vk::Pipeline pipeline;
	vk::PipelineLayout pipeline_layout;

	std::vector<Particle> drops;

	struct vert {
#include "../shaders/rain.vert.inl"
	};
	struct frag {
#include "../shaders/rain.frag.inl"
	};

	static constexpr int max_count = 1 << 14;

	struct vertex {
		fs::v4f32 position;
	};

	void create(fs::Graphics& gfx, VkRenderPass rp) {
		VmaAllocationCreateInfo ai = {};
		ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
		ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
		VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };

		bi.size = max_count * 4 * sizeof(vertex);
		bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		vmaCreateBuffer(gfx.allocator, &bi, &ai, &vertex_buffer, &vertex_allocation, nullptr);

		bi.size = max_count * 6 * sizeof(fs::u16);
		bi.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		ai.usage = VMA_MEMORY_USAGE_AUTO;
		ai.flags = 0;
		vmaCreateBuffer(gfx.allocator, &bi, &ai, &index_buffer, &index_allocation, nullptr);

		static constexpr char index_list[] = {
			0, 1, 2, 2, 3, 0,
		};

		fs::u16* id = new fs::u16[max_count * 6];
		int u = 0;
		for_n(q, max_count)
			FS_FOR(6) id[u++] = q*4 + index_list[i];
		gfx.upload_buffer(index_buffer, id, bi.size);
		delete [] id;

		Pipeline_Layout_Creator{}
			.add_push_range(VK_SHADER_STAGE_VERTEX_BIT, sizeof(glm::mat4))
			.create(&pipeline_layout);

		vk::Basic_Vertex_Input<decltype(vertex::position)> vi;
		Pipeline_Creator pc{ rp, pipeline_layout };
		pc	.add_shader(VK_SHADER_STAGE_VERTEX_BIT  , fs::create_shader(gfx.device, vert::size, vert::data))
			.add_shader(VK_SHADER_STAGE_FRAGMENT_BIT, fs::create_shader(gfx.device, frag::size, frag::data))
			.vertex_input(&vi)
			.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
			.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR);

		pc.blend_attachment.blendEnable = VK_TRUE;
		pc.blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
		pc.blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
		pc.blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		pc.blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		pc.blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		pc.blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		
		pc.depth_stencil_state.depthWriteEnable = VK_FALSE;

		pc.create_and_destroy_shaders(&pipeline);
	}

	void destroy(fs::Graphics& gfx) {
		vmaDestroyBuffer(gfx.allocator, vertex_buffer, vertex_allocation);
		vmaDestroyBuffer(gfx.allocator, index_buffer, index_allocation);
	}

	void add_quad(vertex*& vd, fs::v3f32 center, fs::v3f32 tangent) {
		auto up = fs::v3f32(0.0f, 0.2f, 0.0f);
		*vd++ = vertex{{center - tangent + up, 0.0f}};
		*vd++ = vertex{{center - tangent - up, 1.0f}};
		*vd++ = vertex{{center + tangent - up, 1.0f}};
		*vd++ = vertex{{center + tangent + up, 0.0f}};
	}

	void generate(Camera_Controller& cam, float dt) {
		auto view = cam.get_view_direction();
	//	auto tangent = 0.0025f * fs::v3f32::from(glm::cross(glm::normalize(glm::vec3(view.x, 0.0f, view.z)), glm::vec3(0.0, 1.0, 0.0)));

		auto pos = cam.get_position();
		int const N = 40;

		if (drops.size() > 16'000) drops.erase(drops.begin(), drops.begin()+N);
		
		FS_FOR(N) {
		//	float d = 2.0f*cam.view_rotation.x + (2.0f * float(rand()) / float(RAND_MAX) - 1.0f);
			float d = float FS_TAU * float(rand()) / float(RAND_MAX);
			float r = 0.3f + 10.0f * float(rand()) / float(RAND_MAX);
			drops.emplace_back(fs::v3f32(pos.x + r*sinf(d), pos.y + 5.0f, pos.z + r*cosf(d)));
		}

		vertex* vd;
		vmaMapMemory(engine.graphics.allocator, vertex_allocation, (void**)&vd);
		for (auto&& [p] : drops) {
			p.y -= 5.0f * dt;
			auto tangent = glm::cross(glm::vec3(p.x, p.y, p.z) - pos, glm::vec3(0.0, 1.0, 0.0));
			tangent = 0.0025f * glm::normalize(tangent);
			add_quad(vd, p, fs::v3f32::from(tangent));
		}

		vmaUnmapMemory(engine.graphics.allocator, vertex_allocation);
		vmaFlushAllocation(engine.graphics.allocator, vertex_allocation, 0, drops.size()*4*sizeof(fs::v3f32));
	}

	void draw(fs::Render_Context* ctx, Camera_Controller& cam, float dt) {
		generate(cam, dt);

		auto cmd = ctx->command_buffer;
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		auto transform = cam.get_transform();
		vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &transform);

		VkDeviceSize offset = 0;
		vkCmdBindIndexBuffer(cmd, index_buffer, 0, VK_INDEX_TYPE_UINT16);
		vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &offset);

		vkCmdDrawIndexed(cmd, drops.size() * 6, 1, 0, 0, 0);
	}
};
#endif

struct Thread_Info {
	std::mutex mutex;
	std::condition_variable cv;
	std::vector<Mesh>* meshes;
	fs::v2f32 offset;
	World* world;
	bool alive = true;
	bool done = false;
};

void thread_main(Thread_Info* info) {
	while (info->alive) {
		{
			std::unique_lock lock{ info->mutex };
			info->cv.wait(lock);

			removethis::xoff = info->offset.x;
			removethis::yoff = info->offset.y;
			generate_meshes_from_world(*info->world, *info->meshes);
			info->done = true;
		}
		info->cv.notify_one();
	}
}

class Game_Scene : public fs::Scene
{
public:
	Game_Scene() {
		app_load_data::load(camera_controller);
		camera_controller.position = glm::vec3(0.0f);
		wr.create();
	//	outline_technique.create(engine.graphics);
#if RAIN
		rain.create(engine.graphics, outline_technique.render_pass);
#endif
		world = std::make_unique<World>();
		thread_info.meshes = &meshes;
		thread_info.world = world.get();
		thread = std::jthread(thread_main, &thread_info);
	
		generate_meshes_from_world(*world, meshes);
		wr.add_meshes(meshes);
	}
	virtual ~Game_Scene() override {
#if RAIN
		rain.destroy(engine.graphics);
#endif
		thread_info.alive = false;
		thread_info.cv.notify_one();
		wr.destroy();
		app_load_data::save(camera_controller);
	//	outline_technique.destroy(engine.graphics);
	}

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
				wireframe = !wireframe;
			else if (e.key_down.key_id == fs::keys::K)
				post_fx_enable = !post_fx_enable;
			else if (e.key_down.key_id == fs::keys::J)
				wireframe_depth = !wireframe_depth;

			else if (e.key_down.key_id == fs::keys::Up)
				_amplitude += 0.1f;
			else if (e.key_down.key_id == fs::keys::Down)
				_amplitude -= 0.1f;
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
		_time += dt;
		camera_controller.update(dt);
		
		auto& position = camera_controller.position;
		fs::u32 changed = 0;
		if (position.x <-1.0f) { changed |= 1; offset.x -= 1.0f; }
		if (position.x > 1.0f) { changed |= 2; offset.x += 1.0f; }
		if (position.z <-1.0f) { changed |= 4; offset.y -= 1.0f; }
		if (position.z > 1.0f) { changed |= 8; offset.y += 1.0f; }

#if 0
		if (changed) {
			removethis::xoff = offset.x;
			removethis::yoff = offset.y;
			generate_meshes_from_world(*world, meshes);
			wr.vertex_count = 0;
			wr.mesh_count = 0;
			wr.add_meshes(meshes);
		}
#else
		if (change != 0 && changed) {
			std::unique_lock lock{ thread_info.mutex };
			thread_info.cv.wait_until(lock, nullptr, [&] { return thread_info.done == true; });
		}

		if (thread_info.done) {
			std::scoped_lock lock{ thread_info.mutex };
			if (change & 1) position.x += 1.0f;
			if (change & 2) position.x -= 1.0f;
			if (change & 4) position.z += 1.0f;
			if (change & 8) position.z -= 1.0f;
			change = 0;

			wr.vertex_count = 0;
			wr.mesh_count = 0;
			wr.add_meshes(meshes);
			thread_info.done = false;
		}
		if (changed) {
			std::scoped_lock lock{ thread_info.mutex };
			thread_info.offset = offset;
			thread_info.cv.notify_one();
			change = changed;
		}
#endif
#if RAIN
		rain.draw(ctx, camera_controller, dt);
#endif
		wr.draw(ctx, camera_controller, wireframe);

		auto P = glm::ivec3(glm::floor(camera_controller.position));
		auto C = camera_controller.get_chunk_position();
		engine.debug_layer.add("Position: %i,%i,%i  Chunk: %i,%i,%i", P.x, P.y, P.z, C.x, C.y, C.z);
		engine.debug_layer.add("render wireframe: %s", FS_BTF(wireframe));
		float FOV = camera_controller.field_of_view;
		engine.debug_layer.add("FOV: %.2f (%.1f deg)", FOV, FOV * (360.0f/float(FS_TAU)));
		engine.debug_layer.add("number of quads: %i", total_number_of_quads);
		auto total_mib = double(total_vertex_gpu_memory)/double(1024*1024);
		auto usage = double(100 * used_vertex_gpu_memory) / double(total_vertex_gpu_memory);
		engine.debug_layer.add("GPU memory usage: %.2f%% / %.3f MiB", usage, total_mib);
		engine.debug_layer.add("render distance: %.0f", world->render_distance);
		engine.debug_layer.add("offset: %.1f", thread_info.offset.x);
	}

	virtual void on_resize() override {
		wr.resize_frame_buffers();
	}
private:
	Camera_Controller camera_controller;
	std::unique_ptr<World> world;
#if RAIN
	Rain rain;
#endif
	bool wireframe = false;
	bool wireframe_depth = true;
	bool post_fx_enable  = RAIN? false:true;

	World_Renderer wr;
	std::vector<Mesh> meshes;

	Thread_Info thread_info;
	std::jthread thread;

	fs::v2f32 offset;
	fs::u32 change = 0;
};

fs::Scene* on_create_scene(fs::Scene_Key const& key) {
//	int constexpr N = 1'000'000'000;
//	std::vector<int> random_array;
//	random_array.resize(N);
//	struct Work {
//		int* dst;
//		int count;
//	};
//	auto generate = [](Work& w) {
//		FS_FOR (w.count) {
//			w.dst[i] = rand();
//		}
//	};
//	job_queue<Work> job_queue{8, generate};
//
//	double start = fs::timestamp();
//#if 1
//	FS_FOR(8) job_queue.add_work(random_array.data() + (i*(N/8)), N/8);
//	job_queue.wait();
//#else
//	FS_FOR(N) random_array.data()[i] = rand();
//#endif
//	double time_took = fs::seconds_elasped(start, fs::timestamp());
//
//	char buffer[128];
//	snprintf(buffer, sizeof(buffer), "error: time took: %f\n", time_took);
//	OutputDebugStringA(buffer);
//
//	return nullptr;
	return new Game_Scene;
}

fs::Defaults on_create() {
	engine.app_name = FS_str("\"something\"");
	engine.app_version = {2, 2, 0};
	engine.app_version_info = FS_str("ok");
	static auto title = std::format("minecraft clone [{}]", engine.get_version_string().str());
	return {
		.window_title = FS_str_std(title),
	};
}

