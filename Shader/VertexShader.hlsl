cbuffer object : register(b0)
{
    float4x4 gWorld;
}

cbuffer camera : register(b1)
{
    float4x4 gViewProj;
}

void VS(in float3 inPosL : POSITION, out float4 outPosH : SV_POSITION)
{
    float4 posW = mul(float4(inPosL, 1.0f), gWorld);
    outPosH = mul(posW, gViewProj);
}