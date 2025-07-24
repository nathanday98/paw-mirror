struct Command
{
	float2 start;
	float2 end;
	float2 min_uv;
	float2 max_uv;
	float2 clip_position;
	float2 clip_size;
	float4 color;
	uint texture_index;
	float thickness;
};
struct VertexOutput
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
	float2 uv : TEXCOORD;
	float2 position_in_clip_rect_norm : TEXCOORD1;
	uint texture_index : BLENDINDICES;

};

struct DrawConstants
{
	float4x4 view_to_clip;
	uint buffer_index;
	uint buffer_offset_bytes;
	uint sampler_index;
};

float2 rightPerp(float2 vec)
{
	return float2(vec.y, -vec.x);
}

float2 leftPerp(float2 vec)
{
	return float2(-vec.y, vec.x);
}

ConstantBuffer<DrawConstants> draw_constants : register(b0);

VertexOutput VSMain(uint index : SV_VertexID)
{
	ByteAddressBuffer command_buffer = ResourceDescriptorHeap[draw_constants.buffer_index];
	const Command command = command_buffer.Load<Command>(((index / 6) * sizeof(Command)) + draw_constants.buffer_offset_bytes);
	const float2 dir = normalize(command.end - command.start);
	const float2 left_dir = leftPerp(dir) * command.thickness * 0.5;
	const float2 right_dir = rightPerp(dir) * command.thickness * 0.5;

	float2 const top_left = command.start + left_dir;
	float2 const top_right = command.start + right_dir;
	float2 const bottom_left = command.end + left_dir;
	float2 const bottom_right = command.end + right_dir;

	const float2 verts[] = {
		top_left,
		top_right,
		bottom_left,

		bottom_left,
		top_right,
		bottom_right,
	};

	const float2 uvs[] = {
		float2(command.min_uv.x, command.min_uv.y),
		float2(command.max_uv.x, command.min_uv.y),
		float2(command.min_uv.x, command.max_uv.y),

		float2(command.min_uv.x, command.max_uv.y),
		float2(command.max_uv.x, command.min_uv.y),
		float2(command.max_uv.x, command.max_uv.y),
	};

	float2 const position_in_view = verts[index % 6];
	float2 const position_in_clip_rect_norm = (position_in_view - command.clip_position) / command.clip_size; 

	VertexOutput result;
	result.position = mul(draw_constants.view_to_clip, float4(position_in_view, 0.0, 1.0));
	result.color = command.color;
	result.uv = uvs[index % 6];
	result.texture_index = command.texture_index;
	result.position_in_clip_rect_norm = position_in_clip_rect_norm;
	return result;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
	if(input.position_in_clip_rect_norm.x < 0.0f || input.position_in_clip_rect_norm.y < 0.0f || input.position_in_clip_rect_norm.x > 1.0f || input.position_in_clip_rect_norm.y > 1.0f) {
		discard;
	}
	float4 color = float4(1.0, 1.0, 1.0, 1.0);
	if (input.texture_index > 0)
	{
		const SamplerState sampler = SamplerDescriptorHeap[draw_constants.sampler_index];
		const Texture2D texture = ResourceDescriptorHeap[NonUniformResourceIndex(input.texture_index)];
		color = texture.Sample(sampler, input.uv);
	}
	return input.color * color;
}