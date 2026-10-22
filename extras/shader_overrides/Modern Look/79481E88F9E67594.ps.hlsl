// Modern Look - puddle water (game shader 79481E88F9E67594).
//
// The original mixes two screen-projected textures by viewing angle: tf3 is
// the reflection (strongest at grazing angles, plus a specular highlight)
// and tf5 the view into the water (strongest looking straight down). Its
// vertex shader (69BE68CC048883BA) passes everything in the surface's
// tangent space: r0 view direction, r1 normal, r3 light direction, r2 the
// screen projection (r2.w = view depth) and r4/r5 fog.
//
// This version keeps that and adds, from the F6 > Shader sliders:
//   - ripples: small animated waves anchored in the world (rebuilt from the
//     screen position, view depth and the camera the game passes in),
//   - distortion of the reflection and the view into the water by them,
//   - a stronger, sharper reflection at grazing angles (fresnel),
//   - a sharp glint of the light on the ripples,
//   - a depth tint that darkens and cools the water seen straight down.

#include "xe_interface.hlsli"

#define DEFAULT_RIPPLES      0.50
#define DEFAULT_RIPPLE_SIZE  1.00
#define DEFAULT_RIPPLE_SPEED 1.00
#define DEFAULT_DISTORTION   0.50
#define DEFAULT_REFLECTION   1.00
#define DEFAULT_GLINT        1.00
#define DEFAULT_DEPTH_TINT   0.30

struct Water {
  float ripples, size, speed, distortion, reflection, glint, tint;
};

// Slots 15-21 of the Modern Look preset.ini; XeUser(63) says they are live.
Water LoadWater() {
  Water w;
  if (XeUser(63) == 1.0) {
    w.ripples = XeUser(15);
    w.size = max(XeUser(16), 0.05);
    w.speed = XeUser(17);
    w.distortion = XeUser(18);
    w.reflection = XeUser(19);
    w.glint = XeUser(20);
    w.tint = XeUser(21);
  } else {
    w.ripples = DEFAULT_RIPPLES;
    w.size = DEFAULT_RIPPLE_SIZE;
    w.speed = DEFAULT_RIPPLE_SPEED;
    w.distortion = DEFAULT_DISTORTION;
    w.reflection = DEFAULT_REFLECTION;
    w.glint = DEFAULT_GLINT;
    w.tint = DEFAULT_DEPTH_TINT;
  }
  return w;
}

float3 XeVec(uint first) { return float3(XeUser(first), XeUser(first + 1), XeUser(first + 2)); }

// World position of this pixel from the camera the game passes in
// (shader_settings.h): eye + depth * (forward + x * right + y * up).
float3 WorldPosition(float2 screen_uv, float depth) {
  float2 ndc = float2(screen_uv.x * 2.0 - 1.0, 1.0 - screen_uv.y * 2.0);
  return XeVec(48) + depth * (XeVec(51) + ndc.x * XeVec(54) + ndc.y * XeVec(57));
}

// Slope of a sum of travelling waves over the ground plane, in world units.
// Each wave: direction, wavelength (world units), amplitude.
float2 RippleSlope(float2 p, float time, float size, float speed) {
  static const float4 kWaves[6] = {
      float4(0.94, 0.34, 31.0, 0.040), float4(-0.57, 0.82, 19.0, 0.035),
      float4(0.21, -0.98, 13.0, 0.030), float4(-0.86, -0.51, 8.5, 0.022),
      float4(0.66, 0.75, 5.5, 0.016), float4(-0.12, 0.99, 3.7, 0.012)};
  float2 slope = 0.0;
  [unroll] for (int i = 0; i < 6; ++i) {
    float2 dir = kWaves[i].xy;
    float wavelength = kWaves[i].z * size;
    float k = 6.2831853 / wavelength;
    // Deep-water dispersion: longer waves travel faster.
    float omega = sqrt(9.8 * 100.0 * k) * 0.25 * speed;
    float phase = dot(dir, p) * k - time * omega;
    slope += dir * (kWaves[i].w * wavelength * k) * cos(phase);
  }
  return slope;
}

XeOutput main(XeInput input) {
  XeOutput output;
  Water w = LoadWater();
  bool enhanced = XeUser(63) != 2.0;
  bool fog = C3.x == C255.z;

  float2 screen_uv = input.r2.xy / input.r2.w;
  float3 V = normalize(input.r0.xyz);
  float3 N = normalize(input.r1.xyz);
  float3 L = normalize(input.r3.xyz);

  // ---- Ripples: bend the normal in tangent space (z is the surface normal).
  float2 slope = 0.0;
  float3 world = 0.0;
  bool have_world = enhanced && XeUser(61) == 1.0;
  if (have_world) {
    world = WorldPosition(screen_uv, input.r2.w);
    slope = RippleSlope(world.xz, XeUser(60), w.size, w.speed) * w.ripples;
    // Fade ripples out with distance, where they would only shimmer.
    slope *= saturate(1.5 - input.r2.w / 3000.0);
    N = normalize(N - float3(slope, 0.0));
  }

  // ---- Distorted lookups: shift by the bent normal, less with distance.
  float2 offset = slope * (w.distortion * 0.06) / max(input.r2.w / 300.0, 1.0);
  float4 refl = XeSample2D(3, screen_uv + offset);
  float4 under = XeSample2D(5, screen_uv + offset * 0.6);

  // ---- The original lighting.
  float n_dot_l = max(dot(N, L), C255.y);
  float3 diffuse = (C0.xyz + n_dot_l * C1.xyz) * C2.xyz;
  float3 H = normalize(V + L);
  float n_dot_h = saturate(dot(H, N));
  float3 specular = pow(n_dot_h, C255.x) * C1.xyz;
  float facing = saturate(dot(V, N) * (1.0 + C255.z));

  if (enhanced) {
    // Fresnel: reflection rises steeply at grazing angles.
    float fresnel = pow(1.0 - saturate(dot(V, N)), 5.0);
    facing = saturate(facing - fresnel * w.reflection);
    // Glint: a much tighter highlight that sparkles on the ripples.
    specular += pow(n_dot_h, 400.0) * C1.xyz * (w.glint * 4.0);
    // Depth tint: the view into the water darkens and cools.
    under.rgb *= lerp(1.0, float3(0.55, 0.68, 0.75), w.tint);
  }

  float3 color = lerp(diffuse * refl.rgb + specular, diffuse * under.rgb, facing);
  float alpha = lerp(refl.a, under.a, facing);
  if (fog) {
    color = color * input.r4.xyz + input.r5.xyz;
  }

  output.oC0 = XeOutputColor(0, float4(color, alpha));
  return output;
}
