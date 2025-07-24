struct VertexOutput
{
	float4 position : SV_Position;
	float3 color : COLOR;
	float2 uv : TEXCOORD0;
};

struct BufferData
{
	float3 color;
};

struct DrawConstants
{
	float3 color;
	uint buffer_index;
	uint buffer_offset;
	uint texture_index;
	uint sampler_index;
	//float one_over_aspect_ratio;
};

ConstantBuffer<DrawConstants> draw_constants : register(b0);

VertexOutput VSMain(uint index : SV_VertexID)
{
	const float2 vertices[] =
	{
		float2(-0.5 /* * draw_constants.one_over_aspect_ratio */, -0.5),
		float2(0.0, 0.5),
		float2(0.5 /* * draw_constants.one_over_aspect_ratio */, -0.5),
	};
	
	const float3 colors[] =
	{
		float3(1.0, 0.0, 0.0),
		float3(0.0, 1.0, 0.0),
		float3(0.0, 0.0, 1.0),
	};

	const float2 uvs[] =
	{
		float2(0.0, 0.0),
		float2(0.5, 1.0),
		float2(1.0, 0.0),
	};
	
	VertexOutput output;
	output.position = float4(vertices[index], 0.0, 1.0);
	output.color = draw_constants.color;
	output.uv = uvs[index];
	return output;
}

struct PSOutput
{
	float4 color : SV_Target0;
	float4 color2 : SV_Target1;
};

PSOutput PSMain(VertexOutput input) : SV_Target
{
	ByteAddressBuffer buffer = ResourceDescriptorHeap[draw_constants.buffer_index];
	BufferData buffer_data = buffer.Load<BufferData>(draw_constants.buffer_offset);

	Texture2D<float4> texture = ResourceDescriptorHeap[draw_constants.texture_index];
	SamplerState sampler = SamplerDescriptorHeap[draw_constants.sampler_index];
	float4 tex_data = texture.Sample(sampler, input.uv);

	PSOutput result;
	//result.color = float4(buffer_data.color, 1.0); 
	result.color = tex_data * float4(buffer_data.color, 1.0);
	result.color2 = float4(input.color.x, 0.0, input.color.y, 1.0);
	return result; 

}