# MY VULKAN ENGINE
It is fully raytraced. For now only software-based raytracing.
## DIFFERENT BINDINGS

**Bold** means that user will not be able to affect this directly.

Binding 0: **renderScreen**.

Binding 1: **Matrix array**.

Binding 2: Triangle data.
```c
struct triangleData {
    vec3 points[3];
    vec4 color;
    int textureIndex; //-1 means no texture

    vec3 normals[3];
    int normalTextureIndex; //-1 means no normal texture
    uint materialIndex;
};
```

Binding 3: Sphere data.
```c
struct sphereData {
    vec3 position;
    float radius;
    vec4 color;
    int textureIndex;
    int normalTextureIndex; 
    uint materialIndex;
};
```

Binding 4: Texture array.

Binding 5: Normal texture array.

Binding 6: Material array.
```c
struct Material {
    float roughness;
    float luminosity;
    float refraction;
    float emmisivity;
    vec4 color;
};
```
