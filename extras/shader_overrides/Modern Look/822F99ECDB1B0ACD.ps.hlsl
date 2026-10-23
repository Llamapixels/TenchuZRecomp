// Modern Look - an all-in-one replacement for Tenchu Z's final screen pass.
//
// Game shader 822F99ECDB1B0ACD combines the frame with the game's blur, depth
// of field and bloom textures. This file does exactly what the original did,
// then adds on top of it:
//   1. Sharpening      (contrast-adaptive, like AMD CAS)
//   2. Clarity         (local contrast)
//   3. Bloom           (built from the frame itself, two sizes)
//   4. Tone mapping    (a soft shoulder so bright areas roll off instead of clipping)
//   5. Colour grading  (exposure, contrast, saturation, vibrance, warmth)
//   6. Vignette
//   7. Dithering       (hides banding in dark gradients)
//
// Install: copy the "Modern Look" folder into shader_overrides\ next to
// tenchuz.exe and pick it in F6 > Shader > Preset. A typo is safe: the game
// logs the error and uses its original shader.
//
// Tuning: F6 > Shader has live sliders for everything below (preset.ini).
// The values here are only used without preset.ini; changing them then
// needs a restart.

// --------------------------------------------------------------- defaults

#define DEFAULT_SHARPEN          0.50   // 0 off .. 1 strong
#define DEFAULT_CLARITY          0.35   // 0 off .. 1 strong
#define DEFAULT_BLOOM            0.40   // 0 off .. 2 very glowy
#define DEFAULT_BLOOM_THRESHOLD  0.60   // brightness where bloom starts (0..1)
#define DEFAULT_BLOOM_SIZE       1.00   // radius multiplier
#define DEFAULT_BLOOM_HAZE       0.40   // 0 tight glow only .. 1 wide haze only
#define DEFAULT_HIGHLIGHTS       0.80   // highlights above this roll off (1 = off)
#define DEFAULT_EXPOSURE         1.00
#define DEFAULT_CONTRAST         1.12   // 1 = unchanged
#define DEFAULT_SATURATION       1.15   // 1 = unchanged, 0 = greyscale
#define DEFAULT_VIBRANCE         0.20   // boosts dull colours more than vivid ones
#define DEFAULT_WARMTH           0.03   // + warmer, - cooler
#define DEFAULT_SHADOW_LIFT      0.00   // raise blacks (0..0.15)
#define DEFAULT_VIGNETTE         0.30   // 0 off .. 1 strong
#define DEFAULT_COMPARE          0      // 1 = original image on the left half
#define DEFAULT_AO               0.80   // ambient occlusion strength (0 off .. 2)
#define DEFAULT_AO_RADIUS        60.0   // world units (about cm)
#define DEFAULT_FOG              0.35   // atmospheric haze amount (0 off .. 1)
#define DEFAULT_FOG_DISTANCE     6000.0 // world units over which the haze builds up
#define DEFAULT_FOG_TINT        -0.30   // -1 blue .. 0 neutral .. 1 warm
#define DEFAULT_DOF              0.40   // background blur (0 off .. 1)
#define DEFAULT_DOF_DISTANCE     3.00   // blur starts at this many times the focus distance
#define DEFAULT_SHAFTS           0.50   // light shafts (0 off .. 2)
#define DEFAULT_SHAFTS_LENGTH    0.50   // how far the shafts reach (fraction of the screen)

// Not on the sliders.
// The depth texture (tf3) holds about k / view depth: large up close, small
// far away (it is linear across the screen on flat ground). k is measured
// every frame at the player, whose distance the game passes in
// (XeUser(40..43)); this is used while the player is off screen.
#define DEPTH_SCALE_FALLBACK 10.0
#define SHAFT_SOURCE       float2(0.5, -0.15)  // screen point the shafts come from
#define BLOOM_KNEE         0.25   // softness of the bloom threshold
#define BLOOM_RADIUS       0.025  // tight glow radius (fraction of screen height)
#define BLOOM_HAZE_RADIUS  0.08   // wide haze radius
#define BLOOM_TINT         float3(1.00, 0.97, 0.92)
#define DITHER             1      // 0 off, 1 on

// ------------------------------------------------------------ the shader

#include "xe_interface.hlsli"

struct Settings {
  float sharpen, clarity, bloom, threshold, bloom_size, haze, highlights;
  float exposure, contrast, saturation, vibrance, warmth, lift, vignette;
  bool compare;
  float ao, ao_radius, fog, fog_distance, fog_tint, dof, dof_distance;
  float shafts, shafts_length;
};

// XeUser(n) are the F6 > Shader sliders, in this order. XeUser(63) is 1 when
// they are live, 2 when the effects are switched off, 0 without the menu.
Settings LoadSettings() {
  Settings s;
  if (XeUser(63) == 1.0) {
    s.sharpen = XeUser(0);
    s.clarity = XeUser(1);
    s.bloom = XeUser(2);
    s.threshold = XeUser(3);
    s.bloom_size = XeUser(4);
    s.haze = XeUser(5);
    s.highlights = XeUser(6);
    s.exposure = XeUser(7);
    s.contrast = XeUser(8);
    s.saturation = XeUser(9);
    s.vibrance = XeUser(10);
    s.warmth = XeUser(11);
    s.lift = XeUser(12);
    s.vignette = XeUser(13);
    s.compare = XeUser(14) != 0.0;
    s.ao = XeUser(15);
    s.ao_radius = max(XeUser(16), 1.0);
    s.fog = XeUser(17);
    s.fog_distance = max(XeUser(18), 1.0);
    s.fog_tint = XeUser(19);
    s.dof = XeUser(20);
    s.dof_distance = max(XeUser(21), 1.0);
    s.shafts = XeUser(22);
    s.shafts_length = XeUser(23);
  } else {
    s.sharpen = DEFAULT_SHARPEN;
    s.clarity = DEFAULT_CLARITY;
    s.bloom = DEFAULT_BLOOM;
    s.threshold = DEFAULT_BLOOM_THRESHOLD;
    s.bloom_size = DEFAULT_BLOOM_SIZE;
    s.haze = DEFAULT_BLOOM_HAZE;
    s.highlights = DEFAULT_HIGHLIGHTS;
    s.exposure = DEFAULT_EXPOSURE;
    s.contrast = DEFAULT_CONTRAST;
    s.saturation = DEFAULT_SATURATION;
    s.vibrance = DEFAULT_VIBRANCE;
    s.warmth = DEFAULT_WARMTH;
    s.lift = DEFAULT_SHADOW_LIFT;
    s.vignette = DEFAULT_VIGNETTE;
    s.compare = DEFAULT_COMPARE != 0;
    s.ao = DEFAULT_AO;
    s.ao_radius = DEFAULT_AO_RADIUS;
    s.fog = DEFAULT_FOG;
    s.fog_distance = DEFAULT_FOG_DISTANCE;
    s.fog_tint = DEFAULT_FOG_TINT;
    s.dof = DEFAULT_DOF;
    s.dof_distance = DEFAULT_DOF_DISTANCE;
    s.shafts = DEFAULT_SHAFTS;
    s.shafts_length = DEFAULT_SHAFTS_LENGTH;
  }
  return s;
}

float3 SceneAt(float2 uv);
float3 BrightPass(float3 c, float threshold);

float RawDepthAt(float2 uv) {
  return XeApplyTextureSigns(
             3, XeTexture2D_fc3.SampleLevel(XeSampler_fc3, float3(uv, 0.0), 0.0)).x;
}

static float g_depth_scale = DEPTH_SCALE_FALLBACK;

// k for this frame: player distance x the texture's value at the player. The
// largest of five nearby samples is used, so a gap (background showing
// between arms and body) does not throw it off.
float MeasureDepthScale(float2 texel) {
  float2 at = float2(XeUser(40), XeUser(41));
  float2 o = texel * 6.0;
  float v = RawDepthAt(at);
  v = max(v, RawDepthAt(at + float2(o.x, 0.0)));
  v = max(v, RawDepthAt(at - float2(o.x, 0.0)));
  v = max(v, RawDepthAt(at + float2(0.0, o.y)));
  v = max(v, RawDepthAt(at - float2(0.0, o.y)));
  return (XeUser(43) == 1.0 && v > 1e-6) ? XeUser(42) * v : DEPTH_SCALE_FALLBACK;
}

// View depth in world units.
float DepthAt(float2 uv) {
  return g_depth_scale / max(RawDepthAt(uv), 1e-6);
}

// Ambient occlusion from depth alone. Pairs of opposite neighbours are
// compared with the pixel: on a flat surface their average depth matches
// it, in a crease or where objects meet the ground both are closer. A
// neighbour far in front (a separate object) is ignored, so outlines do not
// get dark halos. Returns 0 (open) .. 1 (fully occluded).
float AmbientOcclusion(float2 uv, float d, float radius, float tan_y, float aspect,
                       float noise) {
  float r_uv = min(radius / (2.0 * d * tan_y), 0.08);  // fraction of screen height
  float occlusion = 0.0;
  [unroll] for (int i = 0; i < 6; ++i) {
    float a = (i + noise) * (3.14159265 / 6.0);
    float r = r_uv * (0.3 + 0.7 * frac(noise * 3.7 + i * 0.618));
    float2 o = float2(cos(a) * aspect, sin(a)) * r;
    float d1 = DepthAt(uv + o);
    float d2 = DepthAt(uv - o);
    float crease = (d - 0.5 * (d1 + d2)) / radius;
    float in_range = saturate(2.0 - max(d - d1, d - d2) / radius);
    occlusion += saturate(crease * 1.5) * in_range;
  }
  // Far away the radius is under a pixel: nothing to measure.
  return occlusion / 6.0 * step(0.0015, r_uv);
}

// Light shafts: bright, distant pixels smeared from SHAFT_SOURCE toward this
// one, fading along the way.
float3 LightShafts(float2 uv, float length_fraction, float threshold, float noise) {
  float2 step_uv = (SHAFT_SOURCE - uv) * (length_fraction / 12.0);
  float2 p = uv + step_uv * noise;
  float3 sum = 0.0;
  float decay = 1.0;
  [unroll] for (int i = 0; i < 12; ++i) {
    p += step_uv;
    float far = saturate((DepthAt(p) - 5000.0) / 5000.0);  // beyond ~50 m
    sum += BrightPass(SceneAt(p), threshold) * far * decay;
    decay *= 0.88;
  }
  return sum / 12.0;
}

float3 SceneAt(float2 uv) {
  return XeApplyTextureSigns(
      0, XeTexture2D_fc0.SampleLevel(XeSampler_fc0, float3(uv, 0.0), 0.0)).rgb;
}

float Luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

// Soft-knee bright pass: only the part of a colour above the threshold glows.
float3 BrightPass(float3 c, float threshold) {
  float peak = max(c.r, max(c.g, c.b));
  float soft = clamp(peak - threshold + BLOOM_KNEE, 0.0, 2.0 * BLOOM_KNEE);
  soft = soft * soft / (4.0 * BLOOM_KNEE + 1e-5);
  return c * (max(soft, peak - threshold) / max(peak, 1e-5));
}

// Bloom over a disc of 16 taps (Vogel spiral), rotated per pixel by `noise`
// so the few taps blur into fine grain instead of visible copies. `average`
// receives the plain blurred colour of the same disc (used for clarity).
float3 BloomDisc(float2 uv, float2 radius, float noise, float threshold,
                 out float3 average) {
  float3 sum = 0.0;
  average = 0.0;
  float weights = 0.0;
  [unroll] for (int i = 0; i < 16; ++i) {
    float r = sqrt((i + 0.5) / 16.0);
    float a = i * 2.39996323 + noise * 6.28318531;
    float w = 1.0 - 0.75 * r * r;
    float3 c = SceneAt(uv + float2(cos(a), sin(a)) * r * radius);
    sum += BrightPass(c, threshold) * w;
    average += c * w;
    weights += w;
  }
  average /= weights;
  return sum / weights;
}

// Per channel: unchanged below the shoulder, then an exponential roll-off
// that approaches 1 without ever clipping.
float3 Shoulder(float3 x, float s) {
  float range = max(1.0 - s, 1e-4);
  float3 rolled = s + range * (1.0 - exp(-(x - s) / range));
  return (x < s || s >= 1.0) ? x : rolled;
}

float3 Grade(float3 c, Settings s) {
  c *= s.exposure;
  c = Shoulder(c, s.highlights);
  c = c + s.lift * (1.0 - c);
  c = (c - 0.5) * s.contrast + 0.5;
  float y = Luma(c);
  float vivid = max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b));
  c = lerp(y, c, s.saturation + s.vibrance * (1.0 - saturate(vivid * 2.0)));
  c *= float3(1.0 + s.warmth, 1.0, 1.0 - s.warmth);
  return saturate(c);
}

XeOutput main(XeInput input) {
  XeOutput output;
  float2 uv = input.r0.xy;
  Settings set = LoadSettings();

  // One screen pixel in texture coordinates. (GetDimensions does not return
  // the frame's size for this texture, so take it from the screen instead.)
  float2 texel = abs(float2(ddx(uv.x), ddy(uv.y)));

  // ---- The original shader, unchanged in behaviour.
  float4 scene = XeSample2D(0, uv);
  float4 blur = XeSample2D(2, uv);
  float depth = XeSample2D(3, uv).x;
  float4 game_bloom = XeSample2D(6, uv);
  float4 glare = XeSample2D(7, uv);
  float4 blur2 = XeSample2D(8, uv);

  float4 blurred = C3.x != 0.0 ? lerp(blur, blur2, C255.y) : blur + blur2 * C0.x;
  float focus = depth - C4.x;
  float dof = saturate(focus * focus * C255.x * C5.x);

  // ---- 1. Sharpening, applied to the sharp frame before it is mixed with blur.
  float3 sharp = scene.rgb;
  if (set.sharpen > 0.0) {
    float3 n = SceneAt(uv - float2(0.0, texel.y));
    float3 s = SceneAt(uv + float2(0.0, texel.y));
    float3 w = SceneAt(uv - float2(texel.x, 0.0));
    float3 e = SceneAt(uv + float2(texel.x, 0.0));
    float3 lo = min(sharp, min(min(n, s), min(w, e)));
    float3 hi = max(sharp, max(max(n, s), max(w, e)));
    float3 amount = sqrt(saturate(min(lo, 1.0 - hi) / max(hi, 1e-4)));
    float3 k = -amount * lerp(0.125, 0.2, saturate(set.sharpen));
    sharp = saturate((sharp + (n + s + w + e) * k) / (1.0 + 4.0 * k));
  }

  float3 original = scene.rgb;
  if (C2.x != 0.0) {
    original.gb *= C255.z;
    sharp.gb *= C255.z;
  }
  original = lerp(original, blurred.rgb, dof);
  float3 color = lerp(sharp, blurred.rgb, dof);
  float alpha = blurred.a;
  if (C1.x != 0.0) {
    float4 extra = game_bloom * C255.y + glare;
    original += extra.rgb;
    color += extra.rgb;
    alpha += extra.a;
  }

  // Effects switched off in F6: the game's original image.
  if (XeUser(63) == 2.0) {
    output.oC0 = XeOutputColor(0, float4(original, alpha));
    return output;
  }

  // Per-pixel noise (interleaved gradient noise) for bloom rotation and dither.
  float noise = frac(52.9829189 * frac(dot(input.xe_position.xy, float2(0.06711056, 0.00583715))));

  float aspect_x = texel.x / texel.y;
  g_depth_scale = MeasureDepthScale(texel);
  float d = DepthAt(uv);
  float tan_y = XeUser(61) == 1.0 ? length(float3(XeUser(57), XeUser(58), XeUser(59))) : 0.42;

  // ---- Ambient occlusion: darken creases and contact areas.
  if (set.ao > 0.0) {
    float occlusion = AmbientOcclusion(uv, d, set.ao_radius, tan_y, aspect_x, noise);
    color *= 1.0 - saturate(occlusion * set.ao);
  }

  // ---- 2 + 3. Clarity, bloom, depth of field and haze share two blurred
  // copies of the frame: a small one (near_avg) and a wide one (far_avg).
  if (set.bloom > 0.0 || set.clarity > 0.0 || set.dof > 0.0 || set.fog > 0.0) {
    float2 aspect = float2(aspect_x, 1.0) * set.bloom_size;
    float3 near_avg, far_avg;
    float3 glow = BloomDisc(uv, aspect * BLOOM_RADIUS, noise, set.threshold, near_avg);
    float3 haze = BloomDisc(uv, aspect * BLOOM_HAZE_RADIUS, frac(noise + 0.5), set.threshold,
                            far_avg);

    // Depth of field: focus on what is at the middle of the screen (the
    // player), blur what is well behind it.
    if (set.dof > 0.0) {
      float focus = XeUser(43) == 1.0 ? XeUser(42) : DepthAt(float2(0.5, 0.55));
      float start = focus * set.dof_distance;
      float amount = saturate((d - start) / start) * set.dof;
      color = lerp(color, lerp(near_avg, far_avg, saturate(amount * 2.0 - 1.0)),
                   saturate(amount * 2.0));
    }

    // Clarity: push each pixel away from its surroundings' average, scaled
    // down in deep shadow and bright highlights so neither clips.
    float y = Luma(color);
    float midtones = saturate(y * 4.0) * saturate((1.0 - y) * 2.0);
    color += (color - near_avg) * set.clarity * midtones;
    color += lerp(glow, haze, set.haze) * BLOOM_TINT * set.bloom;

    // Atmospheric haze: fades distant things toward the surrounding light,
    // tinted cool or warm.
    if (set.fog > 0.0) {
      float amount = (1.0 - exp(-d / set.fog_distance)) * set.fog;
      float3 tint = lerp(float3(0.80, 0.92, 1.20), float3(1.20, 1.00, 0.80),
                         saturate(set.fog_tint * 0.5 + 0.5));
      float3 fog_color = lerp(Luma(far_avg).xxx, far_avg, 0.5) * tint;
      color = lerp(color, fog_color, saturate(amount));
    }
  }

  // ---- Light shafts from bright, distant areas.
  if (set.shafts > 0.0) {
    color += LightShafts(uv, set.shafts_length, set.threshold, noise) * BLOOM_TINT * set.shafts;
  }

  // ---- 4 + 5. Tone mapping and grading. The game's colour may be stored
  // linear (gamma render target); grade it in display space either way.
  bool linear_target = (XeSysUint(XE_SYS_FLAGS) & XE_FLAG_COLOR0_TO_GAMMA) != 0;
  if (linear_target) color = pow(max(color, 0.0), 1.0 / 2.2);
  color = Grade(max(color, 0.0), set);

  // ---- 6. Vignette.
  float2 centered = uv - 0.5;
  color *= saturate(1.0 - dot(centered, centered) * set.vignette * 1.8);

  // ---- 7. Dither by +-half a step of an 8-bit display.
  if (DITHER) color += (noise - 0.5) / 255.0;

  if (linear_target) color = pow(saturate(color), 2.2);

  if (set.compare) {
    if (uv.x < 0.5) color = original;
    if (abs(uv.x - 0.5) < 2.0 * texel.x) color = 1.0;  // white divider
  }

  output.oC0 = XeOutputColor(0, float4(color, alpha));
  return output;
}
