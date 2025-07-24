struct VertexOutput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD;
};

struct DrawConstants
{
	uint image_index;
	uint sampler_index;
	float offset;
};

ConstantBuffer<DrawConstants> draw_constants : register(b0);

VertexOutput VSMain(uint index : SV_VertexID)
{
	const float2 verts[] = {
		float2(draw_constants.offset + -1.0, 1.0),
		float2(draw_constants.offset + 1.0, 1.0),
		float2(draw_constants.offset + -1.0, -1.0),
		float2(draw_constants.offset + -1.0, -1.0),
		float2(draw_constants.offset + 1.0, 1.0),
		float2(draw_constants.offset + 1.0, -1.0),
	};

	const float2 uvs[] = {
		float2(0.0, 0.0),
		float2(1.0, 0.0),
		float2(0.0, 1.0),
		float2(0.0, 1.0),
		float2(1.0, 0.0),
		float2(1.0, 1.0),
	};

	VertexOutput result;
	result.position = float4(verts[index], 0.0, 1.0);
	result.uv = uvs[index];
	return result;
}

float4 PSMain(VertexOutput input) : SV_TARGET
{
	Texture2D<float4> texture = ResourceDescriptorHeap[draw_constants.image_index];
	SamplerState sampler = SamplerDescriptorHeap[draw_constants.sampler_index];
	float4 result = texture.Sample(sampler, input.uv);
	return result;
}