#version 140

const vec3 ZERO = vec3(0.0, 0.0, 0.0);
//BBS: add grey and orange
//const vec3 GREY = vec3(0.9, 0.9, 0.9);
const vec3 ORANGE = vec3(0.8, 0.4, 0.0);
const vec3 LightRed = vec3(0.78, 0.0, 0.0);
const vec3 LightBlue = vec3(0.73, 1.0, 1.0);
const float EPSILON = 0.0001;

struct PrintVolumeDetection
{
	// 0 = rectangle, 1 = circle, 2 = custom, 3 = invalid
	int type;
    // type = 0 (rectangle):
    // x = min.x, y = min.y, z = max.x, w = max.y
    // type = 1 (circle):
    // x = center.x, y = center.y, z = radius
	vec4 xy_data;
    // x = min z, y = max z
	vec2 z_data;
};

struct ColorBedExcludeArea
{
	int enable;
	vec4 xy_data;
	vec2 z_data;
};

struct SlopeDetection
{
    bool actived;
	float normal_z;
    mat3 volume_world_normal_matrix;
};

uniform vec4 uniform_color;
uniform bool use_color_clip_plane;
uniform vec4 uniform_color_clip_plane_1;
uniform vec4 uniform_color_clip_plane_2;
uniform SlopeDetection slope;

//BBS: add outline_color
uniform bool is_outline;

#ifdef ENABLE_ENVIRONMENT_MAP
    uniform sampler2D environment_tex;
    uniform bool use_environment_tex;
#endif // ENABLE_ENVIRONMENT_MAP

uniform PrintVolumeDetection print_volume;
uniform ColorBedExcludeArea color_bed_exclude_area;

in vec3 clipping_planes_dots;
in float color_clip_plane_dot;

// x = diffuse, y = specular;
in vec2 intensity;

in vec4 world_pos;
in float world_normal_z;
in vec3 eye_normal;

out vec4 out_color;

// Roost Print: the shop light, as on rapidroost.net's 3D preview. The website lights models with
// the shop's 360 light (turned so the counter runs square to the plate), a warm key light, a satin
// PLA surface (roughness 0.48, a little clearcoat) and AgX tone mapping. The light's numbers are
// baked in below by tools/roost/studio_light.py (colour-balanced for the slicer, see there); the
// maths matches three.js's physical material.
uniform bool roost_studio;
in vec3 roost_world_normal;
in vec3 roost_world_view;

// ROOST_STUDIO_BEGIN (written by tools/roost/studio_light.py from the website's shop studio; don't edit)
const vec3 ROOST_SH0 = vec3(1.388085, 1.265934, 1.231996);
const vec3 ROOST_SH1 = vec3(-0.263031, -0.215589, -0.236507);
const vec3 ROOST_SH2 = vec3(-0.138406, 0.072749, 0.127531);
const vec3 ROOST_SH3 = vec3(0.019720, -0.004610, -0.079738);
const vec3 ROOST_SH4 = vec3(-0.066565, -0.067461, -0.055462);
const vec3 ROOST_SH5 = vec3(-0.007682, -0.014948, -0.020971);
const vec3 ROOST_SH6 = vec3(0.130565, 0.119939, 0.080925);
const vec3 ROOST_SH7 = vec3(-0.002723, -0.002298, -0.006929);
const vec3 ROOST_SH8 = vec3(-0.024413, -0.023158, -0.002836);
const vec3 ROOST_KEY_DIR = vec3(0.149101, -0.598702, 0.786972);
const vec3 ROOST_KEY_COLOR = vec3(3.300000, 2.637039, 2.059069);
// ROOST_STUDIO_END

const float ROOST_PI = 3.14159265;
const float ROOST_ROUGHNESS = 0.48;

// The 360 light seen by a matte surface facing n (irradiance / pi).
vec3 roost_shop(vec3 n)
{
    return ROOST_SH0 * 0.282095
         + ROOST_SH1 * (0.488603 * n.y) + ROOST_SH2 * (0.488603 * n.z) + ROOST_SH3 * (0.488603 * n.x)
         + ROOST_SH4 * (1.092548 * n.x * n.y) + ROOST_SH5 * (1.092548 * n.y * n.z)
         + ROOST_SH6 * (0.315392 * (3.0 * n.z * n.z - 1.0)) + ROOST_SH7 * (1.092548 * n.x * n.z)
         + ROOST_SH8 * (0.546274 * (n.x * n.x - n.y * n.y));
}

vec3 roost_to_linear(vec3 c)
{
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

vec3 roost_to_srgb(vec3 c)
{
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

// three.js AgXToneMapping (exposure 1) with AgX's punchy look, linear sRGB in and out.
vec3 roost_agx(vec3 color)
{
    mat3 srgb_to_2020 = mat3(vec3(0.6274, 0.0691, 0.0164), vec3(0.3293, 0.9195, 0.0880), vec3(0.0433, 0.0113, 0.8956));
    mat3 rec2020_to_srgb = mat3(vec3(1.6605, -0.1246, -0.0182), vec3(-0.5876, 1.1329, -0.1006), vec3(-0.0728, -0.0083, 1.1187));
    mat3 inset = mat3(vec3(0.856627153315983, 0.137318972929847, 0.11189821299995),
                      vec3(0.0951212405381588, 0.761241990602591, 0.0767994186031903),
                      vec3(0.0482516061458583, 0.101439036467562, 0.811302368396859));
    mat3 outset = mat3(vec3(1.1271005818144368, -0.1413297634984383, -0.14132976349843826),
                       vec3(-0.11060664309660323, 1.157823702216272, -0.11060664309660294),
                       vec3(-0.016493938717834573, -0.016493938717834257, 1.2519364065950405));
    float min_ev = -12.47393;
    float max_ev = 4.026069;
    color = inset * (srgb_to_2020 * color);
    color = clamp((log2(max(color, vec3(1e-10))) - min_ev) / (max_ev - min_ev), 0.0, 1.0);
    vec3 x2 = color * color;
    vec3 x4 = x2 * x2;
    color = 15.5 * x4 * x2 - 40.14 * x4 * color + 31.96 * x4 - 6.868 * x2 * color + 0.4298 * x2 + 0.1191 * color - 0.00232;
    // The "punchy" look (Filament's agxLook / Blender's AgX Punchy): keeps filament colours rich.
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = pow(max(color, vec3(0.0)), vec3(1.35));
    color = luma + 1.4 * (color - luma);
    color = outset * color;
    color = pow(max(vec3(0.0), color), vec3(2.2));
    return clamp(rec2020_to_srgb * color, 0.0, 1.0);
}

// three.js DFGApprox / EnvironmentBRDF for a dielectric (F0 = 0.04).
float roost_env_brdf(float n_dot_v, float roughness)
{
    vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * n_dot_v)) * r.x + r.y;
    vec2 fab = vec2(-1.04, 1.04) * a004 + r.zw;
    return 0.04 * fab.x + fab.y;
}

// A filament colour (sRGB) lit by the shop, back to sRGB for the screen.
vec3 roost_shade(vec3 srgb)
{
    vec3 albedo = roost_to_linear(srgb);
    vec3 n = normalize(roost_world_normal);
    if (!gl_FrontFacing)
        n = -n;
    vec3 v = normalize(roost_world_view);
    float n_dot_v = clamp(dot(n, v), 0.0001, 1.0);

    // Key light: Lambert + GGX (three.js BRDF_GGX with V_GGX_SmithCorrelated).
    vec3 l = ROOST_KEY_DIR;
    float n_dot_l = max(dot(n, l), 0.0);
    vec3 h = normalize(l + v);
    float n_dot_h = max(dot(n, h), 0.0);
    float v_dot_h = max(dot(v, h), 0.0);
    float a = ROOST_ROUGHNESS * ROOST_ROUGHNESS;
    float a2 = a * a;
    float d = n_dot_h * n_dot_h * (a2 - 1.0) + 1.0;
    float D = a2 / (ROOST_PI * d * d);
    float gv = n_dot_l * sqrt(n_dot_v * n_dot_v * (1.0 - a2) + a2);
    float gl = n_dot_v * sqrt(n_dot_l * n_dot_l * (1.0 - a2) + a2);
    float V = 0.5 / max(gv + gl, 0.000001);
    float F = 0.04 + 0.96 * pow(1.0 - v_dot_h, 5.0);
    vec3 key_diffuse = ROOST_KEY_COLOR * (n_dot_l / ROOST_PI);
    vec3 key_specular = ROOST_KEY_COLOR * (n_dot_l * F * V * D);

    // The shop all round: soft light, and its blurred reflection.
    float spec_energy = roost_env_brdf(n_dot_v, ROOST_ROUGHNESS);
    vec3 shop_diffuse = max(roost_shop(n), vec3(0.0)) * (1.0 - spec_energy);
    vec3 shop_specular = max(roost_shop(reflect(-v, n)), vec3(0.0)) * spec_energy;

    // 1.2: the satin PLA's thin clearcoat adds a fifth again to the shine.
    vec3 color = albedo * (shop_diffuse + key_diffuse) + 1.2 * (key_specular + shop_specular);
    return roost_to_srgb(roost_agx(color));
}

void main()
{
    if (any(lessThan(clipping_planes_dots, ZERO-vec3(0.002))))
        discard;

    vec4 color;
	if (use_color_clip_plane) {
		color.rgb = (color_clip_plane_dot < 0.0) ? uniform_color_clip_plane_1.rgb : uniform_color_clip_plane_2.rgb;
		color.a = uniform_color.a;
    }
    else
	    color = uniform_color;

    if (slope.actived) {
         if(world_pos.z<0.1&&world_pos.z>-0.1)
         {
                color.rgb = LightBlue;
                color.a = 0.8;
         }
         else if( world_normal_z < slope.normal_z - EPSILON)
         {
                color.rgb = color.rgb * 0.5 + LightRed * 0.5;
                color.a = 0.8;
         }
    }
    // if the fragment is outside the print volume -> use darker color
	vec3 pv_check_min = ZERO;
	vec3 pv_check_max = ZERO;
    bool outside = false;
    if (print_volume.type == 0) {
		// rectangle
		pv_check_min = world_pos.xyz - vec3(print_volume.xy_data.x, print_volume.xy_data.y, print_volume.z_data.x);
		pv_check_max = world_pos.xyz - vec3(print_volume.xy_data.z, print_volume.xy_data.w, print_volume.z_data.y);
	}
	else if (print_volume.type == 1) {
		// circle
		float delta_radius = print_volume.xy_data.z - distance(world_pos.xy, print_volume.xy_data.xy);
		pv_check_min = vec3(delta_radius, 0.0, world_pos.z - print_volume.z_data.x);
		pv_check_max = vec3(0.0, 0.0, world_pos.z - print_volume.z_data.y);
	}
	outside = (any(lessThan(pv_check_min, ZERO)) || any(greaterThan(pv_check_max, ZERO)));
	
    if (!outside && color_bed_exclude_area.enable == 1) {
		pv_check_min = world_pos.xyz - vec3(color_bed_exclude_area.xy_data.x, color_bed_exclude_area.xy_data.y, color_bed_exclude_area.z_data.x);
		pv_check_max = vec3(color_bed_exclude_area.xy_data.z, color_bed_exclude_area.xy_data.w, color_bed_exclude_area.z_data.y) - world_pos.xyz;
        outside = (all(greaterThan(pv_check_min, ZERO)) && all(greaterThan(pv_check_max, ZERO)));
    }

    color.rgb = outside ? mix(color.rgb, ZERO, 0.3333) : color.rgb;

    //BBS: add outline_color
    if (is_outline)
        out_color = uniform_color;
#ifdef ENABLE_ENVIRONMENT_MAP
    else if (use_environment_tex)
        out_color = vec4(0.45 * texture(environment_tex, normalize(eye_normal).xy * 0.5 + 0.5).xyz + 0.8 * color.rgb * intensity.x, color.a);
#endif
    else if (roost_studio)
        out_color = vec4(roost_shade(color.rgb), color.a); // Roost Print: the shop light
    else
        out_color = vec4(vec3(intensity.y) + color.rgb * intensity.x, color.a);
}