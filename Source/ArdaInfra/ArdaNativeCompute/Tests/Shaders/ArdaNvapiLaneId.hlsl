#define NV_SHADER_EXTN_SLOT u0
#define NV_SHADER_EXTN_REGISTER_SPACE space1
#include "nvHLSLExtns.h"

RWByteAddressBuffer Output : register(u0);
cbuffer Constants : register(b0)
{
	uint Base;
};

[numthreads(32, 1, 1)]
void main(uint3 Id : SV_DispatchThreadID)
{
	Output.Store(Id.x * 4, Base + NvGetLaneId());
}
