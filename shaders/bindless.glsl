#ifdef PT_D3D12
layout(set = 0, binding = 3) uniform sampler tex_sampler;
#define PT_TEXTURE2D_ARRAY texture2D
#define PT_TEXTURECUBE_ARRAY textureCube
#define PT_TEX2D(i) sampler2D(textures[nonuniformEXT(i)], tex_sampler)
#define PT_TEXCUBE(i) samplerCube(cube_textures[nonuniformEXT(i)], tex_sampler)
#else
#define PT_TEXTURE2D_ARRAY sampler2D
#define PT_TEXTURECUBE_ARRAY samplerCube
#define PT_TEX2D(i) textures[nonuniformEXT(i)]
#define PT_TEXCUBE(i) cube_textures[nonuniformEXT(i)]
#endif
