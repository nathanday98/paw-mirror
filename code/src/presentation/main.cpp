#include "core/std.h"
#include <core/assert.h>
#include <core/gfx.h>
#include <core/logger.h>
#include <core/memory.inl>
#include <core/arena.h>
#include <core/math.h>
#include <core/platform.h>

PAW_DISABLE_ALL_WARNINGS_BEGIN
#include <ft2build.h>
#include FT_FREETYPE_H
PAW_DISABLE_ALL_WARNINGS_END

#include "ui/ui_test.h"

void AssertFunc(char const* file, U32 line, char const* expression, char const* message)
{
	PAW_ERROR("Assert: %s\n\tFile: %s\n\tLine: %d\n\tExpression: %s\n", message, file, line, expression);
}

CoreAssertFunc* g_core_assert_func = &AssertFunc;

extern int PlatformMain(int arg_count, char* args[]);

struct SceneViewPass : Gfx::GraphExecutor
{
	Gfx::GraphTexture color_rt;
	Gfx::GraphTexture color1_rt;
	Gfx::GraphTexture depth_rt;
	Gfx::Pipeline test_pso;
	Float3 color;
	Gfx::Texture texture;
	Gfx::Sampler sampler;

	static SceneViewPass const& Build(Gfx::GraphBuilder& builder, StringView8 name, S32 width, S32 height, Gfx::Pipeline pso, Float3 color, Gfx::Texture texture, Gfx::Sampler sampler)
	{
		Gfx::GraphPass& pass = Gfx::GraphCreatePass(builder, name);
		SceneViewPass& data = Gfx::GraphSetExecutor<SceneViewPass>(builder, pass);
		data.color_rt = Gfx::GraphCreateTexture(builder, pass, {
																   Gfx::Access::RenderTarget,
																   width,
																   height,
																   Gfx::Format::R16G16B16A16_Float,
																   PAW_STR("Scene Color RT"),
																   Gfx::InitialState::Clear,
																   {.color = {0.33f, 0.33f, 0.33f, 1.0f}},
															   });
		data.color1_rt = Gfx::GraphCreateTexture(builder, pass, {
																	Gfx::Access::RenderTarget,
																	width,
																	height,
																	Gfx::Format::R16G16B16A16_Float,
																	PAW_STR("Scene Color1 RT"),
																	Gfx::InitialState::Clear,
																	{.color = {1.0f, 1.0f, 1.0f, 1.0f}},
																});
		data.depth_rt = Gfx::GraphCreateTexture(builder, pass, {
																   Gfx::Access::Depth,
																   width,
																   height,
																   Gfx::Format::Depth32_Float,
																   PAW_STR("Scene Depth RT"),
																   Gfx::InitialState::Clear,
																   {.depth_stencil = {.depth = 0.0f, .stencil = 0}},
															   });
		data.test_pso = pso;
		data.color = color;
		data.texture = texture;
		data.sampler = sampler;
		return data;
	}

	void Execute(Gfx::State& gfx_state, Gfx::CommandList command_list) override
	{
		Gfx::BindPipeline(gfx_state, command_list, test_pso);

		struct BufferData
		{
			Float3 color;
		};

		Gfx::BufferAlloc<BufferData> buffer_alloc = Gfx::AllocTempBuffer<BufferData>(gfx_state);
		buffer_alloc.ptr->color = color;

		PAW_ERROR_ON_PADDING_BEGIN struct DrawConstants
		{
			Float3 color;
			Gfx::BufferDescriptor buffer_index;
			U32 buffer_offset_bytes;
			Gfx::TextureDescriptor texture_index;
			Gfx::SamplerDescriptor sampler_index;
			// float one_over_aspect_ratio;
		};
		PAW_ERROR_ON_PADDING_END
		static_assert(sizeof(DrawConstants) % 4 == 0);
		DrawConstants const constants{
			.color = {1.0f, 0.0f, 0.0f},
			.buffer_index = buffer_alloc.descriptor,
			.buffer_offset_bytes = buffer_alloc.offset_bytes,
			.texture_index = Gfx::GetTextureDescriptor(gfx_state, texture),
			.sampler_index = Gfx::GetSamplerDescriptor(gfx_state, sampler),
		};

		Gfx::SetConstants(gfx_state, command_list, constants);
		Gfx::SetTopology(gfx_state, command_list, Gfx::Topology::Triangles);
		Gfx::Draw(gfx_state, command_list, 3, 1, 0, 0);
	}
};

struct BlitPass : Gfx::GraphExecutor
{
	Gfx::GraphTexture input_texture;
	Gfx::GraphTexture backbuffer;
	Gfx::Pipeline pso;

	Gfx::Sampler sampler;

	static BlitPass const& Build(Gfx::GraphBuilder& builder, Gfx::GraphTexture output_texture, Gfx::GraphTexture input_texture, Gfx::Sampler sampler, Gfx::Pipeline pso)
	{
		Gfx::GraphPass& pass = Gfx::GraphCreatePass(builder, PAW_STR("Blit"));
		BlitPass& data = Gfx::GraphSetExecutor<BlitPass>(builder, pass);
		data.input_texture = Gfx::GraphReadTexture(builder, pass, input_texture, Gfx::Access::PixelShader);
		data.backbuffer = Gfx::GraphWriteTexture(builder, pass, output_texture, Gfx::Access::RenderTarget);
		data.sampler = sampler;
		data.pso = pso;
		return data;
	}

	void Execute(Gfx::State& gfx_state, Gfx::CommandList command_list) override
	{

		Gfx::BindPipeline(gfx_state, command_list, pso);

		PAW_ERROR_ON_PADDING_BEGIN
		struct DrawConstants
		{
			Gfx::TextureDescriptor image_index;
			Gfx::SamplerDescriptor sampler_index;
			F32 offset;
		};
		PAW_ERROR_ON_PADDING_END
		DrawConstants const constants{
			.image_index = Gfx::GetGraphTextureDescriptor(gfx_state, input_texture),
			.sampler_index = Gfx::GetSamplerDescriptor(gfx_state, sampler),
			.offset = 0.0f,
		};
		Gfx::SetConstants(gfx_state, command_list, constants);
		Gfx::SetTopology(gfx_state, command_list, Gfx::Topology::Triangles);
		Gfx::Draw(gfx_state, command_list, 6, 1, 0, 0);
	}
};

struct GuiGlyph
{
	Float2 size;
	Float2 min_uv;
	Float2 max_uv;
	Float2 offset;
	F32 advance;
	bool colored;
};

struct UIPass : Gfx::GraphExecutor
{
	Gfx::GraphTexture output;
	Gfx::Pipeline pso;
	Gfx::Sampler sampler;
	Gfx::Texture font_texture;
	Slice<GuiGlyph> glyph_data;
	FT_Face font_face;
	Float2 viewport_size;
	F32 dpi;
	Slice<RenderItem const> render_items;

	static UIPass& Build(Gfx::GraphBuilder& builder, Gfx::GraphTexture write_texture, Gfx::Pipeline pso, Gfx::Sampler sampler, Gfx::Texture font_texture, Float2 viewport_size, Slice<GuiGlyph> glyph_data, FT_Face font_face)
	{
		Gfx::GraphPass& pass = Gfx::GraphCreatePass(builder, PAW_STR("UI"));
		UIPass& data = Gfx::GraphSetExecutor<UIPass>(builder, pass);
		data.output = Gfx::GraphWriteTexture(builder, pass, write_texture, Gfx::Access::RenderTarget);
		data.pso = pso;
		data.sampler = sampler;
		data.font_texture = font_texture;
		data.viewport_size = viewport_size;
		data.glyph_data = glyph_data;
		data.font_face = font_face;
		// #TODO: Handle dpi properly
		data.dpi = 1.0f;
		return data;
	}

	PAW_ERROR_ON_PADDING_BEGIN
	struct Command
	{
		Float2 start{};
		Float2 end{};
		Float2 min_uv{0.0f, 0.0f};
		Float2 max_uv{1.0f, 1.0f};
		Float2 clip_position{};
		Float2 clip_size{};
		Float4 color{1.0f, 1.0f, 1.0f, 1.0f};
		Gfx::TextureDescriptor texture_index{0};
		F32 thickness{5.0f};
	};
	PAW_ERROR_ON_PADDING_END

	void DrawText2D(Float2 position, StringView8 text, Float4 const& color, Slice<Command> commands, S32& command_count, Gfx::TextureDescriptor font_texture_descriptor)
	{
		Float2 pen{};
		Float2 offset{Round(position.x), Round(position.y)};
		U32 unicode = 0;
		U32 byte_count = 0;
		for (PtrSize glyph_index = 0; glyph_index < text.size_bytes; ++glyph_index)
		{
			Byte const b = text.ptr[glyph_index];
			if ((b & 0b11000000) == 0b11000000)
			{
				bool bytes_2 = (b & 0b11100000) == 0b11000000;
				bool bytes_3 = (b & 0b11110000) == 0b11100000;
				bool bytes_4 = (b & 0b11111000) == 0b11110000;
				unicode = 0;
				if (bytes_2)
				{
					unicode |= b & 0b00011111;
					unicode <<= 5;
					byte_count = 1;
				}
				else if (bytes_3)
				{
					unicode |= b & 0b00001111;
					unicode <<= 4;
					byte_count = 2;
				}
				else if (bytes_4)
				{
					unicode |= b & 0b00000111;
					unicode <<= 3;
					byte_count = 3;
				}
			}
			else if ((b & 0b11000000) == 0b10000000)
			{
				// #TODO: Improve this message
				PAW_ASSERT(byte_count > 0, "Byte count is 0");
				byte_count--;
				unicode <<= 6;
				unicode |= b & 0b00111111;
			}
			else
			{
				unicode = b;
				byte_count = 0;
			}

			if (byte_count > 0)
			{
				continue;
			}

			U32 const char_index = FT_Get_Char_Index(font_face, unicode);
			GuiGlyph const& glyph = glyph_data[char_index];
			bool is_space = false;
			if (unicode < 128)
			{
				Byte const ascii = unicode & 0xFF;
				is_space = ascii == ' ' || ascii == '\t' || ascii == '\n' || ascii == '\r';
			}
			F32 const inv_dpi_scale = 1.0f / dpi;
			if (!is_space)
			{
				// This probably still needs some kind of position / size rounding to prevent
				Float2 const pos = offset + pen + glyph.offset * inv_dpi_scale;
				F32 const thickness = (glyph.size.x * inv_dpi_scale * 0.5f);
				PAW_ASSERT(command_count < commands.count, "Ran out of command space");
				commands[command_count++] = {
					.start = pos + Float2{thickness, 0.0f},
					.end = pos + Float2{thickness, glyph.size.y * inv_dpi_scale},
					.min_uv = glyph.min_uv,
					.max_uv = glyph.max_uv,
					.color = glyph.colored ? Float4{1.0f, 1.0f, 1.0f, 1.0f} : color,
					.texture_index = font_texture_descriptor,
					.thickness = thickness * 2.0f,
				};
			}
			pen.x += Round(glyph.advance * inv_dpi_scale);
		}
	}

	void Execute(Gfx::State& gfx_state, Gfx::CommandList command_list) override
	{

		Gfx::BufferSliceAlloc<Command> command_alloc = Gfx::AllocTempBuffer<Command>(gfx_state, render_items.count);
		S32 command_count = 0;

		for (RenderItem const& render_item : render_items)
		{
			switch (render_item.type)
			{
				case RenderItem::Type::Box:
				{
					RenderBox const& box = render_item.box;
					command_alloc.items[command_count++] = {
						.start = box.position + Float2{box.size.x * 0.5f, 0.0f},
						.end = box.position + Float2{box.size.x * 0.5f, box.size.y},
						.clip_position = render_item.clip_rect.position,
						.clip_size = render_item.clip_rect.size,
						.color = box.color,
						.thickness = box.size.x,
					};
				}
				break;
				case RenderItem::Type::Line:
				{
					RenderLine const& line = render_item.line;
					command_alloc.items[command_count++] = {
						.start = line.start,
						.end = line.end,
						.clip_position = render_item.clip_rect.position,
						.clip_size = render_item.clip_rect.size,
						.color = line.color,
						.thickness = line.thickness,
					};
				}
				break;
			}
		}

		F32 const right = viewport_size.x;
		F32 const left = 0.0f;
		F32 const top = 0.0f;
		F32 const bottom = viewport_size.y;
		F32 const near = -1.0f;
		F32 const far = 1.0f;
		F32 const width = right - left;
		F32 const height = top - bottom;
		F32 const depth = far - near;

		Gfx::BindPipeline(gfx_state, command_list, pso);

		PAW_ERROR_ON_PADDING_BEGIN
		struct DrawConstants
		{
			F32 view_to_clip[4][4];
			Gfx::BufferDescriptor buffer_index;
			U32 buffer_offset_bytes;
			Gfx::SamplerDescriptor sampler_index;
		};
		PAW_ERROR_ON_PADDING_END

		DrawConstants const constants{
			.view_to_clip = {
				{2.0f / width, 0.0f, 0.0f, 0.0f},
				{0.0f, 2.0f / height, 0.0f, 0.0f},
				{0.0f, 0.0f, 2.0f / depth, 0.0f},
				{-((right + left) / width), -((top + bottom) / height), 0.0f, 1.0f},
			},
			.buffer_index = command_alloc.descriptor,
			.buffer_offset_bytes = command_alloc.offset_bytes,
			.sampler_index = Gfx::GetSamplerDescriptor(gfx_state, sampler),
		};

		Gfx::SetConstants(gfx_state, command_list, constants);
		Gfx::SetTopology(gfx_state, command_list, Gfx::Topology::Triangles);
		Gfx::Draw(gfx_state, command_list, command_alloc.items.count * 6, 1, 0, 0);
	}
};

struct PresentPass : Gfx::GraphExecutor
{

	static PresentPass const& Build(Gfx::GraphBuilder& builder, Gfx::GraphTexture backbuffer)
	{
		Gfx::GraphPass& pass = Gfx::GraphCreatePass(builder, PAW_STR("Present"));
		PresentPass& data = Gfx::GraphSetExecutor<PresentPass>(builder, pass);
		Gfx::GraphReadTexture(builder, pass, backbuffer, Gfx::Access::Present);
		return data;
	}

	void Execute(Gfx::State&, Gfx::CommandList) override
	{
	}
};

int main(int arg_count, char* args[])
{
	MemoryInit();

	ArenaAllocator static_allocator{};
	ArenaAllocator debug_static_allocator{};
	ArenaAllocator temp_allocator{};

	Platform::Init(&static_allocator, &debug_static_allocator);

	Gfx::State& gfx_state = Gfx::Init(&static_allocator, &debug_static_allocator);

	U32 const white_pixel = 0xFFFFFFFF;
	Gfx::Texture const white_texture = Gfx::CreateTexture(gfx_state, {
																		 .width = 1,
																		 .height = 1,
																		 .lifetme = Gfx::Lifetime::Static,
																		 .debug_name = PAW_STR("White Pixel"),
																		 .data = {(Byte*)&white_pixel, 1 * 1 * 4},
																	 });
	(void)white_texture;
	FT_Library library;
	FT_Error error = FT_Init_FreeType(&library);
	if (error)
	{
		PAW_ERROR("Failed to init FreeType");
	}

	int ft_major, ft_minor, ft_patch;
	FT_Library_Version(library, &ft_major, &ft_minor, &ft_patch);

	FT_Face font_face;

	// error = FT_New_Face(library, "source-data/fonts/DroidSans.ttf", 0, &font_face);
	// char const* font_path = "C:\\Windows\\Fonts\\SEGUIEMJ.ttf";
	char const* font_path = "source-data/fonts/DroidSans.ttf";
	error = FT_New_Face(library, font_path, 0, &font_face);
	if (error)
	{
		PAW_ERROR("Failed to load font %s", font_path);
	}
	else
	{
		PAW_INFO(
			"Loaded font %s (%s)", font_path, FT_HAS_COLOR(font_face) ? "color supported" : "color unsupported");
	}

	// #TODO: Get actual dpi here
	F32 const dpi = 1.0f;

	// error = FT_Set_Char_Size(font_face, 0, 16 << 6, g_window_state.dpi, g_window_state.dpi);
	error = FT_Set_Pixel_Sizes(font_face, 0, static_cast<FT_UInt>(Floor(20.0f * dpi)));

	if (error)
	{
		PAW_ERROR("Failed to set font size");
	}

	// #define FT_CEIL(X) (((X + 63) & -64) / 64)
#define FT_CEIL(x) (x >> 6)

	F32 const font_line_height = static_cast<F32>(FT_CEIL(font_face->size->metrics.height));
	F32 const descender = static_cast<F32>(FT_CEIL(font_face->size->metrics.descender));
	F32 const font_height = (static_cast<F32>(FT_CEIL(font_face->size->metrics.ascender)) + descender);
	(void)font_height;
	(void)font_line_height;
	Slice<GuiGlyph> glyph_data = PAW_NEW_SLICE_IN(&static_allocator, font_face->num_glyphs, GuiGlyph);

	S32 const tex_size = (10 + (static_cast<S32>(font_face->size->metrics.height >> 6))) *
		static_cast<S32>(Ceil(SquareRoot(static_cast<F32>(font_face->num_glyphs))));
	// const PtrSize tex_size = 1024;

	Slice<S32> font_buffer = PAW_NEW_SLICE_IN(&temp_allocator, tex_size * tex_size, S32);

	{
		S32 const padding = 1;
		S32 pen_x = padding;
		S32 pen_y = padding;

		Float2 const texture_size_v = Float2{static_cast<F32>(tex_size), static_cast<F32>(tex_size)};
		Float2 const texel_size = Float2{1.0f, 1.0f} / texture_size_v;

		// void* data_ptr = nullptr;
		// PAW_ASSERT(SUCCEEDED(result));

		// U32* font_buffer_ptr = static_cast<U32*>(data_ptr);
		bool colored = false;
		for (S32 glyph_index = 0; glyph_index < font_face->num_glyphs; glyph_index++)
		{
			FT_Error const ft_error =
				FT_Load_Glyph(font_face, glyph_index, FT_LOAD_RENDER | FT_LOAD_FORCE_AUTOHINT | FT_LOAD_COLOR);
			if (ft_error != 0)
			{
				PAW_ERROR("FreeType: %s", FT_Error_String(ft_error));
			}
			FT_Bitmap const* bitmap = &font_face->glyph->bitmap;

			if (pen_x + static_cast<S32>(bitmap->width) >= tex_size)
			{
				pen_x = 0;
				pen_y += (font_face->size->metrics.height >> 6) + padding;
			}

			switch (bitmap->pixel_mode)
			{
				case FT_PIXEL_MODE_BGRA:
				{
					for (U32 row = 0; row < bitmap->rows; row++)
					{
						for (U32 col = 0; col < bitmap->width; col++)
						{
							Byte const* pixel = bitmap->buffer + (row * bitmap->pitch + col * 4);
							font_buffer[(pen_y + row) * static_cast<S32>(tex_size) + (pen_x + col)] =
								(pixel[3] << 24) | (pixel[0] << 16) | (pixel[1] << 8) | pixel[2];
						}
					}
					colored = true;
				}
				break;

				case FT_PIXEL_MODE_GRAY:
				{
					for (U32 row = 0; row < bitmap->rows; row++)
					{
						for (U32 col = 0; col < bitmap->width; col++)
						{
							Byte const* pixel = bitmap->buffer + (row * bitmap->pitch + col);
							U32 const color = (*pixel << 24) | 0xFFFFFF;
							font_buffer[(pen_y + row) * static_cast<S32>(tex_size) + (pen_x + col)] = color;
						}
					}
				}
				break;

				default:
				{
					PAW_WARNING("Unsupported pixel type");
				}
			}

			Float2 const pen_pos{static_cast<F32>(pen_x), static_cast<F32>(pen_y)};

			GuiGlyph& out_glyph = glyph_data[glyph_index];
			out_glyph.size = Float2{static_cast<F32>(bitmap->width), static_cast<F32>(bitmap->rows)};
			out_glyph.min_uv = pen_pos * texel_size;
			out_glyph.max_uv = out_glyph.min_uv + out_glyph.size * texel_size;

			out_glyph.offset =
				Float2{static_cast<F32>(font_face->glyph->bitmap_left), -static_cast<F32>(font_face->glyph->bitmap_top)};
			/*out_glyph.offset = Float2{
				static_cast<F32>(FT_CEIL(font_face->glyph->metrics.horiBearingX)),
				static_cast<F32>(FT_CEIL(font_face->glyph->metrics.horiBearingY))};*/
			out_glyph.advance = static_cast<F32>(FT_CEIL(font_face->glyph->advance.x));
			out_glyph.colored = colored;
			pen_x += bitmap->width + padding;
		}
	}

	Gfx::Texture font_texture = Gfx::CreateTexture(gfx_state, {
																  .width = tex_size,
																  .height = tex_size,
																  .format = Gfx::Format::R8G8B8A8_Unorm,
																  .lifetme = Gfx::Lifetime::Static,
																  .debug_name = PAW_STR("Font Texture"),
																  .data = {reinterpret_cast<Byte*>(font_buffer.items), CalcTotalSizeBytes(font_buffer)},
															  });

	MemorySlice const test_hlsl = Platform::LoadFileBlocking("source-data/shaders/test.hlsl", &temp_allocator);
	MemorySlice const ui_hlsl = Platform::LoadFileBlocking("source-data/shaders/ui.hlsl", &temp_allocator);
	MemorySlice const fullscreen_blit_hlsl = Platform::LoadFileBlocking("source-data/shaders/fullscreen_blit.hlsl", &temp_allocator);

	Gfx::Pipeline const test_pso = Gfx::CreateGraphicsPipeline(gfx_state, {
																			  .debug_name = PAW_STR("test"),
																			  .blend_states = {{
																				  .enabled = true,
																				  .write_mask = Gfx::ColorMask::All,
																				  .color_src = Gfx::Blend::SrcAlpha,
																				  .color_dest = Gfx::Blend::InvSrcAlpha,
																				  .color_op = Gfx::BlendOp::Add,
																				  .alpha_src = Gfx::Blend::One,
																				  .alpha_dest = Gfx::Blend::InvSrcAlpha,
																				  .alpha_op = Gfx::BlendOp::Add,
																			  }},
																			  .render_target_formats = {
																				  Gfx::Format::R16G16B16A16_Float,
																				  Gfx::Format::R16G16B16A16_Float,
																			  },
																			  .vertex_mem = test_hlsl,
																			  .pixel_mem = test_hlsl,
																		  });
	Gfx::Pipeline const blit_pso = Gfx::CreateGraphicsPipeline(gfx_state, {
																			  .debug_name = PAW_STR("fullscreen blit"),
																			  .rasterizer_state = {
																				  .cull_mode = Gfx::CullMode::None,
																			  },
																			  .vertex_mem = fullscreen_blit_hlsl,
																			  .pixel_mem = fullscreen_blit_hlsl,
																		  });

	Gfx::Pipeline const ui_pso = Gfx::CreateGraphicsPipeline(gfx_state, {
																			.debug_name = PAW_STR("ui"),
																			.blend_states = {{
																				.enabled = true,
																				.write_mask = Gfx::ColorMask::All,
																				.color_src = Gfx::Blend::SrcAlpha,
																				.color_dest = Gfx::Blend::InvSrcAlpha,
																				.color_op = Gfx::BlendOp::Add,
																				.alpha_src = Gfx::Blend::One,
																				.alpha_dest = Gfx::Blend::InvSrcAlpha,
																				.alpha_op = Gfx::BlendOp::Add,
																			}},
																			.render_target_formats = {
																				Gfx::Format::R16G16B16A16_Float,
																				Gfx::Format::R16G16B16A16_Float,
																			},
																			.rasterizer_state = {
																				.cull_mode = Gfx::CullMode::Back,
																			},
																			.vertex_mem = ui_hlsl,
																			.pixel_mem = ui_hlsl,
																		});

	Gfx::Sampler const sampler = Gfx::CreateSampler(gfx_state, {});

	Gfx::GraphBuilder& graph_builder = Gfx::CreateGraphBuilder(gfx_state, &temp_allocator);

	Gfx::GraphTexture const backbuffer = Gfx::GraphGetBackBuffer(graph_builder);

	Int2 const viewport_size = Platform::GetViewportSize();

	SceneViewPass const& scene_view_pass = SceneViewPass::Build(graph_builder, PAW_STR("Scene View"), viewport_size.x, viewport_size.y, test_pso, {1.0f, 0.0f, 1.0f}, font_texture, sampler);

	UIPass& ui_pass = UIPass::Build(graph_builder, scene_view_pass.color_rt, ui_pso, sampler, font_texture, {static_cast<F32>(viewport_size.x), static_cast<F32>(viewport_size.y)}, glyph_data, font_face);

	BlitPass const& blit_to_backbuffer_pass = BlitPass::Build(graph_builder, backbuffer, ui_pass.output, sampler, blit_pso);
	(void)blit_to_backbuffer_pass;
	// Pass& test_pass = graph_builder.AddPass(PAW_STR("Test Pass"), nullptr);
	// graph_builder.WriteTexture(test_pass, scene_color_rt, Gfx::Access::PixelShader);
	PresentPass::Build(graph_builder, blit_to_backbuffer_pass.backbuffer);

	Gfx::BuildGraph(gfx_state, graph_builder);
	Gfx::DestroyGraphBuilder(graph_builder);

	// temp_allocator.FreeAll();

	PlatformMain(arg_count, args);

	UITest(&static_allocator);

	bool running = true;
	while (running)
	{
		bool const should_quit = Platform::PumpEvents();
		if (should_quit)
		{
			break;
		}

		Slice<RenderItem const> ui_render_items = UIUpdate({static_cast<F32>(viewport_size.x), static_cast<F32>(viewport_size.y)});
		ui_pass.render_items = ui_render_items;
		Gfx::Render(gfx_state);
	}

	Gfx::Deinit(gfx_state);

	MemoryDeinit();
}