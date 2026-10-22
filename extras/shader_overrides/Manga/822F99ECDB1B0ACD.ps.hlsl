// Manga - turns Tenchu Z into a manga page (or a colour comic).
//
// Replaces the game's final screen pass (822F99ECDB1B0ACD), keeping what the
// original did (blur, depth of field, the game's bloom), then:
//   1. Ink lines       (edge detection on brightness)
//   2. Tone bands      (the image cut into a few flat shades, like cel shading)
//   3. Screentone      (halftone dots for the in-between shades)
//   4. Paper           (warm off-white instead of pure white)
//   5. Colour          (0 = black and white manga, 1 = colour comic)
//
// Install: copy the "Manga" folder into shader_overrides\ next to tenchuz.exe
// and pick it in F6 > Shader > Preset. All settings are live sliders there
// (preset.ini); the values below are only used without preset.ini.

// --------------------------------------------------------------- defaults

#define DEFAULT_COLOUR          0.00   // 0 manga .. 1 colour comic
#define DEFAULT_INK             0.85   // outline strength
#define DEFAULT_LINE_WIDTH      1.00   // in 1080p pixels (whole screen pixels at any resolution)
#define DEFAULT_LINE_DETAIL     0.50   // 0 only strong outlines .. 1 every detail
#define DEFAULT_TONE_LEVELS     4.00   // number of flat shades (2..6)
#define DEFAULT_TONE_SIZE       4.00   // screentone dot spacing, game pixels
#define DEFAULT_TONE_STRENGTH   1.00   // 0 flat grey .. 1 dots
#define DEFAULT_BRIGHTNESS      1.40   // the game is dark; brighten before toning
#define DEFAULT_CONTRAST        1.20
#define DEFAULT_PAPER           0.60   // 0 pure white .. 1 old yellowed paper
#define DEFAULT_COMPARE         0      // 1 = original image on the left half

// ------------------------------------------------------------ the shader

#include "xe_interface.hlsli"

struct Settings {
  float colour, ink, line_width, line_detail, tone_levels, tone_size;
  float tone_strength, brightness, contrast, paper;
  bool compare;
};

// XeUser(n) are the F6 > Shader sliders, in preset.ini order. XeUser(63) is 1
// when they are live, 2 when the effects are switched off, 0 without them.
Settings LoadSettings() {
  Settings s;
  if (XeUser(63) == 1.0) {
    s.colour = XeUser(0);
    s.ink = XeUser(1);
    s.line_width = XeUser(2);
    s.line_detail = XeUser(3);
    s.tone_levels = XeUser(4);
    s.tone_size = XeUser(5);
    s.tone_strength = XeUser(6);
    s.brightness = XeUser(7);
    s.contrast = XeUser(8);
    s.paper = XeUser(9);
    s.compare = XeUser(10) != 0.0;
  } else {
    s.colour = DEFAULT_COLOUR;
    s.ink = DEFAULT_INK;
    s.line_width = DEFAULT_LINE_WIDTH;
    s.line_detail = DEFAULT_LINE_DETAIL;
    s.tone_levels = DEFAULT_TONE_LEVELS;
    s.tone_size = DEFAULT_TONE_SIZE;
    s.tone_strength = DEFAULT_TONE_STRENGTH;
    s.brightness = DEFAULT_BRIGHTNESS;
    s.contrast = DEFAULT_CONTRAST;
    s.paper = DEFAULT_PAPER;
    s.compare = DEFAULT_COMPARE != 0;
  }
  return s;
}

float3 SceneAt(float2 uv) {
  return XeApplyTextureSigns(
      0, XeTexture2D_fc0.SampleLevel(XeSampler_fc0, float3(uv, 0.0), 0.0)).rgb;
}

float Luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

// Brightness as the player sees it (gamma space), whatever the render target.
float DisplayLuma(float3 c, bool linear_target) {
  float y = Luma(max(c, 0.0));
  return linear_target ? pow(y, 1.0 / 2.2) : y;
}

XeOutput main(XeInput input) {
  XeOutput output;
  float2 uv = input.r0.xy;
  Settings set = LoadSettings();
  bool linear_target = (XeSysUint(XE_SYS_FLAGS) & XE_FLAG_COLOR0_TO_GAMMA) != 0;

  // One screen pixel in texture coordinates.
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
  float3 original = scene.rgb;
  if (C2.x != 0.0) original.gb *= C255.z;
  original = lerp(original, blurred.rgb, dof);
  float alpha = blurred.a;
  if (C1.x != 0.0) {
    float4 extra = game_bloom * C255.y + glare;
    original += extra.rgb;
    alpha += extra.a;
  }

  if (XeUser(63) == 2.0) {
    output.oC0 = XeOutputColor(0, float4(original, alpha));
    return output;
  }

  // ---- 1. Ink lines. Each pixel is inked by how much brighter its
  // brightest neighbour is, so lines sit on the dark side of an edge only
  // (half the width of a two-sided edge filter). Neighbours are a whole
  // number of screen pixels away, so thin lines stay crisp instead of
  // snapping between pixels; Line width is in 1080p pixels.
  float step_pixels = max(1.0, round(set.line_width / (texel.y * 1080.0)));
  float2 o = texel * step_pixels;
  float c = DisplayLuma(SceneAt(uv), linear_target);
  float brightest = c;
  [unroll] for (int i = 0; i < 8; ++i) {
    static const float2 kNeighbours[8] = {
        float2(-1, -1), float2(0, -1), float2(1, -1), float2(-1, 0),
        float2(1, 0),   float2(-1, 1), float2(0, 1),  float2(1, 1)};
    brightest = max(brightest, DisplayLuma(SceneAt(uv + o * kNeighbours[i]), linear_target));
  }
  float edge = (brightest - c) * set.brightness;
  float threshold = lerp(0.3, 0.025, saturate(set.line_detail));
  float ink = smoothstep(threshold, threshold * 1.8, edge) * set.ink;

  // ---- 2. Tone bands.
  float y = DisplayLuma(original, linear_target) * set.brightness;
  y = saturate((y - 0.5) * set.contrast + 0.5);
  float steps = max(round(set.tone_levels), 2.0) - 1.0;
  float tone = round(y * steps) / steps;  // 0 = black .. 1 = paper

  // ---- 3. Screentone: dots on a 45-degree grid, sized so the dot area
  // matches the band's darkness. Solid black and paper white stay solid.
  float rows = 720.0 / max(set.tone_size, 1.0);
  float2 p = uv * float2(texel.y / texel.x, 1.0) * rows;
  p = float2(p.x + p.y, p.y - p.x) * 0.70710678;
  float d = length(frac(p) - 0.5);
  float coverage = 1.0 - tone;
  float radius = sqrt(coverage / 3.14159265);
  float aa = max(fwidth(d), 1e-4);
  float dot_ink = 1.0 - smoothstep(radius - aa, radius + aa, d);
  if (coverage <= 0.0) dot_ink = 0.0;
  if (coverage >= 1.0) dot_ink = 1.0;
  float shade = lerp(tone, 1.0 - dot_ink, saturate(set.tone_strength));

  // ---- 4 + 5. Paper and colour.
  float3 paper = lerp(float3(1.0, 1.0, 1.0), float3(0.96, 0.92, 0.82), saturate(set.paper));
  float3 display = linear_target ? pow(max(original, 0.0), 1.0 / 2.2) : max(original, 0.0);
  float3 hue = display / max(max(display.r, max(display.g, display.b)), 1e-3);
  hue = saturate(lerp(Luma(hue), hue, 1.3));
  float3 manga = paper * shade;
  float3 comic = paper * hue * (0.12 + 0.88 * shade);
  float3 color = lerp(manga, comic, saturate(set.colour)) * (1.0 - ink);

  if (linear_target) color = pow(saturate(color), 2.2);

  if (set.compare) {
    if (uv.x < 0.5) color = original;
    if (abs(uv.x - 0.5) < 2.0 * texel.x) color = 1.0;  // white divider
  }

  output.oC0 = XeOutputColor(0, float4(color, alpha));
  return output;
}
