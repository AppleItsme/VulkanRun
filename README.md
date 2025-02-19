# MY VULKAN ENGINE
It is fully raytraced. For now only software-based raytracing.

use `git clone --recursive`!!


# Maths
There are 2 spaces. Listing them in the order of transformations:
1. **World/View space** represents the position of meshes in the world.
2. **Screen space** represents the pixels that each mesh occupies

## General transformations
 - Rotation function along the plane $e_{ij}$:

```math
R_{ij}(\theta, \vec{w})= e_i(w_icos\space\theta-w_jsin\space\theta)+e_j(w_isin\space\theta+w_jcos\space\theta)+w_ke_k
``` 
$\qquad$ By first doing the rotation around the x axis, then y axis and lastly z axis, we obtain the **omni directional rotation matrix**:

```math
\begin{pmatrix}
\cos\space\theta_y\cos\space\theta_z&-\sin\space\theta_x\sin\space\theta_y\cos\space\theta_z-\cos\space\theta_x\sin\space\theta_z&\sin\space\theta_x\sin\space\theta_z-\cos\space\theta_x\sin\space\theta_y\cos\space\theta_z&0\\
\cos\space\theta_y\sin\space\theta_z&\cos\space\theta_x\cos\space\theta_z-\sin\space\theta_x\sin\space\theta_y\sin\space\theta_z&-\sin\space\theta_x\cos\space\theta_z-\cos\space\theta_x\sin\space\theta_y\sin\space\theta_z&0\\
\sin\space\theta_y&\sin\space\theta_x\cos\space\theta_y&\cos\space\theta_x\cos\space\theta_y&0\\
0&0&0&1
\end{pmatrix}
```
$\qquad$ The GPU will compute the rotation-scale-translation matrix on its own and we will only provide the following input matrix:

```math
\begin{bmatrix}
\theta_x & x_{scale} & x_{pos}\\
\theta_y & y_{scale} & y_{pos}\\
\theta_z & z_{scale} & z_{pos}
\end{bmatrix}
```
## Transformation stages
<!-- Model to World space:

$\qquad$ From model to World space all we have to do is scale, translate and rotate the points along the user defined setup. Hence for each triangle we send the following inputs:

```math
\begin{bmatrix}
\theta_x & x_{scale} & x_{origin}\\
\theta_y & y_{scale} & y_{origin}\\
\theta_z & z_{scale} & z_{origin}
\end{bmatrix}
``` -->
<!-- 
World to View space:

$\qquad$ The world needs to do the inverse of the rotation and translation matrices as opposed to the transformation experienced by the camera. So we take the same matrix and use negative angles and negative translations. Basically we send this:

```math
\begin{bmatrix}
-\theta_x & x_{scale} & -x_{origin}\\
-\theta_y & y_{scale} & -y_{origin}\\
-\theta_z & z_{scale} & -z_{origin}
\end{bmatrix}
``` -->

View to Screen space:

$\qquad$ View to screen space matrix (**NOTE** the x and y components have to have already been divided by the z coordinate before applying this matrix):

```math
\begin{pmatrix}
\frac{bufferHeight}{2} &0 &0 &\frac{bufferWidth}{2}\\
0 & -\frac{bufferHeight}{2} & 0 & \frac{bufferHeight}{2}\\
0 & 0 & 1 & 0\\
0 & 0 & 0 & 1 
\end{pmatrix}
```

# GPU interface

## Structs and data format
**slightly outdated**
For transformations of meshes, we have `TransformationBuffer`:
```c
struct TransformationBuffer {
    vec3 translation;
    vec3 scale;
    vec3 rotation;
}
```
You may recall that in the General transformations section we sent a matrix instead. Well these two are different ways of describing the exact same piece of data so it doesn't matter.

Vertex buffer will store all the vertices that are used by the application. 
Transformations will be stored separately because many primitives are likely to share the same model to world space transformations, and every primitive will have the exact same World to View to Screen space transformations.

Any other data will be stored in the `TriangleBuffer`:
```c
struct TriangleBuffer {
    uvec3 vertexIndices;
    vec2 UVcoords[3]; //one UV coordinate per vertex
    uint materialIndex;
    uint transformationIndex;
};
```

Or the `ElipsoidBuffer`:
```c
struct ElipsoidBuffer {
    vec3 position;
    uint materialIndex;
    uint transformationIndex;
}
```

`materialIndex` field will be an index to a buffer of the following `struct`:
```c
struct MaterialBuffer {
    float roughness;
    float refraction;
    float luminosity;
    vec3 color;
    bool isTexturePresent;
    uint textureIndex;
    bool isNormalPresent;
    uint normalIndex;
}
```
If `isTexturePresent` or `isNormalPresent` are `false`, then `textureIndex` and `normalIndex` are ignored respectively.

## Bindings
buffer | Binding Index
------- | ---------------
`renderScreen` | 0
`SphereBuffer` | 1
`TransformationBuffer` | 2
`MaterialBuffer` | 3
`SunlightBuffer` | 4
`Misc` | 5 <-- TEMPORARY
`Camera` | 6
`SecondaryRayBuffer` | 7
<!-- `TextureBuffer` | 5
`NormalBuffer` | 6 -->
<!-- `TriangleBuffer` | 1 -->