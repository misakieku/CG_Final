# Project Specification: Autonomous OpenGL SOR Modeler & Procedural 3D Maze Engine

## 1. Executive Summary & Dual-Binary Architecture

Build two decoupled, self-contained executables in modern C++20 and Core Profile OpenGL:
1. **`SOR_Modeller.exe`**: Interactive 2D profile editor (ImGui canvas + cubic Bézier smoothing), real-time 3D revolution preview with analytical differential normals, and single-format `.dat` export.
2. **`MazeGame.exe`**: Procedural 3D maze game (DFS recursive backtracker), FPS camera with AABB sliding collision, dead-end treasure quest importing the `.dat` mesh, and an advanced filmic rendering pipeline.

### Core Architecture Philosophy: 100% Zero-External-Asset Deployment
To ensure zero runtime path crashes and 100% self-contained portability (unzip and run anywhere):
* **Zero External Shader Files**: All GLSL shaders are embedded as C++ raw string literals.
* **Zero External Image Files**: All textures (Brick Albedo, POM Height Map, Sobel Normal Map, Puddle Noise) are **procedurally generated on the CPU at startup into byte buffers** and uploaded to OpenGL via `glTexImage2D`.

---

## 2. Toolchain & Dependency Management

### 2.1 Compiler & Standards
* **Language Standard**: ISO C++20 (`-std=c++20` or `/std:c++20`).
* **Build System**: CMake $\ge$ 3.24.
* **Target Platforms**: Windows (MSVC / Clang-cl).

### 2.2 Dependency Strategy: CMake `FetchContent` Only
All third-party libraries must be acquired via CMake `FetchContent`. Zero reliance on system packages or pre-installed SDKs.

| Library | Version / Branch | Purpose |
| :--- | :--- | :--- |
| `glfw` | Master / Latest release | Window creation, context management, mouse/keyboard polling. |
| `glad` | GL Core Profile | OpenGL function loader. |
| `glm` | Master / Latest release | Mathematics (vec2, vec3, vec4, mat4, transforms). |
| `imgui` | Docking branch | Immediate-mode GUI for the 2D profile canvas in `SOR_Modeller`. |

---

## 3. Resource Encapsulation: Strict C++ RAII

Every OpenGL object must be wrapped in a non-copyable, movable C++ RAII class. Manual calls to `glDelete*` in application code are strictly forbidden.

```cpp
namespace gl {
    class Buffer;       // Wraps VBO / EBO (glGenBuffers / glDeleteBuffers)
    class VertexArray;  // Wraps VAO (glGenVertexArrays / glDeleteVertexArrays)
    class Texture;      // Wraps Texture (glGenTextures / glDeleteTextures)
    class Framebuffer;  // Wraps FBO + RBO / Depth Attachment
    class Shader;       // Wraps Program (glCreateProgram / glDeleteProgram)
}
```
* Destructors check valid ID before deletion: `if (id_) { glDelete*(1, &id_); }`
* Move constructor and move assignment transfer ownership and zero the source ID.

---

## 4. Procedural Texture Synthesis (Zero Image Files)

At engine startup, `TextureGenerator` synthesizes all required materials in memory:

```
[ Procedural Texture Generator ]
       │
       ├── Height Map (256x256): Brick pattern + Perlin noise relief
       ├── Normal Map (256x256): Sobel filter over Height Map (dh/dx, dh/dy)
       ├── Albedo Map (256x256): Stone base color modulated by Height & Mortar
       └── Puddle Noise (128x128): Smooth Perlin/Simplex noise for wet spots
```

### 4.1 Wall Material Synthesis
1. **Height Map ($256 \times 256$, R8)**:
   * Generate an alternating brick grid pattern (width 64px, height 32px, mortar gap 4px).
   * Mortar lines receive value $0.0$; brick interiors receive $0.8 + 0.2 \times \text{Noise}(x, y)$.
2. **Normal Map ($256 \times 256$, RGBA8)**:
   * Apply a $3 \times 3$ Sobel filter over the Height Map to compute spatial gradients:
     $$dx = \text{Sobel}_x(H), \quad dy = \text{Sobel}_y(H)$$
     $$\mathbf{N} = \text{normalize}\big(-dx \cdot \text{scale}, \; -dy \cdot \text{scale}, \; 1.0\big)$$
   * Store as tangent-space normal: `RGB = (N * 0.5 + 0.5) * 255`.
3. **Albedo Map ($256 \times 256$, RGBA8)**:
   * Mortar receives dark gray `(60, 60, 60)`; brick surfaces receive earthy brown/stone tones modulated by height.

---

## 5. Subsystem 1: `SOR_Modeller.exe`

A standalone GUI application dedicated to designing 3D revolution surfaces:

```
┌──────────────────────────────────────┬──────────────────────────────────────┐
│  Left: ImGui 2D Profile Canvas       │  Right: OpenGL 3D Real-Time Viewport │
│                                      │                                      │
│  • Click to place / drag points      │  • Revolved 3D Mesh (Blinn-Phong)    │
│  • Piecewise Cubic Bézier Smoothing  │  • Smooth analytical normals         │
│  • Segment Slider: [N = 32]          │  • Turntable / Arcball camera        │
│  • Button: [Export to myModel.dat]   │  • Wireframe toggle checkbox         │
└──────────────────────────────────────┴──────────────────────────────────────┘
```

### 5.1 Bézier Profile & Analytical Differential Normals
1. **Piecewise Cubic Bézier**:
   $$\mathbf{B}(t) = (1-t)^3 \mathbf{P}_0 + 3(1-t)^2 t \mathbf{P}_1 + 3(1-t) t^2 \mathbf{P}_2 + t^3 \mathbf{P}_3, \quad t \in [0, 1]$$
   where each control point $\mathbf{P}_k = (r_k, y_k)$ defines a parametric 2D profile $\big(r(t), y(t)\big)$ with tangent derivative:
   $$\mathbf{B}'(t) = \big(r'(t), y'(t)\big) = 3(1-t)^2(\mathbf{P}_1-\mathbf{P}_0) + 6(1-t)t(\mathbf{P}_2-\mathbf{P}_1) + 3t^2(\mathbf{P}_3-\mathbf{P}_2)$$
2. **Parametric Revolution**:
   * For sampled curve point $\big(r(t), y(t)\big)$ and radial angle $\theta_j = j \frac{2\pi}{M}$:
     $$\mathbf{S}(t, \theta_j) = \big(r(t)\cos\theta_j,\; y(t),\; r(t)\sin\theta_j\big)$$
3. **Analytical Normals (Differential Geometry)**:
   * Derived from the parametric surface partial derivatives $\frac{\partial \mathbf{S}}{\partial t} \times \frac{\partial \mathbf{S}}{\partial \theta}$ (dividing out $r(t) \ge 0$ analytically so poles remain well-defined and horizontal segments $y'(t)=0$ never divide by zero):
     $$\mathbf{N}(t, \theta) = s \cdot \text{normalize}\big(y'(t)\cos\theta, \; -r'(t), \; y'(t)\sin\theta\big)$$
     where $s \in \{+1, -1\}$ ensures outward orientation matching the triangle winding (specifically $s = +1$ when the profile is traversed bottom-to-top, and $-1$ when top-to-bottom). This guarantees 100% seam-free smooth normals across arbitrary profiles.

### 5.2 Compatible `.dat` Export Format
Output preserves strict prefix compatibility with the course standard (`%.6f` floating-point precision to avoid quantization artifacts on normalized coordinates), with an optional trailing `NORMAL` block to carry exact analytical normals into `MazeGame`:
```text
VERTEX = <vertex_count>
x y z
...
FACE = <triangle_count>
i0 i1 i2
...
NORMAL = <vertex_count>   # Optional trailing section
nx ny nz
...
```
* `SOR_Modeller.exe` exports `VERTEX`, `FACE`, and `NORMAL` (analytical normals) to `myModel.dat` in the binary directory.
* `MazeGame.exe` parses `VERTEX` and `FACE`, reads `NORMAL` when present, and automatically falls back to area-weighted smooth vertex normal reconstruction if a legacy `.dat` file without `NORMAL` is loaded.

---

## 6. Subsystem 2: `MazeGame.exe`

A first-person 3D dungeon crawler importing `myModel.dat` as quest collectibles:

### 6.1 Procedural Maze Generation (DFS Recursive Backtracker)
* Generate an $N \times N$ orthogonal grid (default $15 \times 15$).
* Single-seed, fully connected, acyclic maze.
* **Dead-End Spawner**: Identify all cells with exactly 3 wall neighbors. Automatically place the imported SOR object (chalice/trophy) on a stone pedestal inside the dead ends.

### 6.2 First-Person Navigation & Collision
* Smooth WASD camera movement + mouse look (Pitch/Yaw).
* **AABB Sliding Collision**: Circular/box player collider checked against adjacent maze wall blocks, allowing smooth sliding along walls without sticking or penetrating.
* **Win Condition**: Collecting all SOR treasures and reaching the green exit tile displays a victory overlay. Support `[R]` to re-seed the maze instantly.

---

## 7. Shading, SSPR & Filmic Pipeline

```
[ Geometry Pass ] ──► [ Depth Shadow Pass ] ──► [ SSAO Pass ] 
                                                      │
[ ACES Tone Mapping + Gamma ] ◄── [ SSPR Puddle Pass ] ◄──┘
```

### 7.1 Lighting Model: Lambertian Diffuse + Blinn-Phong Specular
* **Diffuse**: $k_d \cdot \max(0, \mathbf{N} \cdot \mathbf{L})$
* **Specular**: $k_s \cdot \max(0, \mathbf{N} \cdot \mathbf{H})^\alpha$, where $\mathbf{H} = \text{normalize}(\mathbf{L} + \mathbf{V})$.
* Point light (torch/flashlight) with quadratic distance attenuation: $1.0 / (1.0 + 0.09 d + 0.032 d^2)$.

### 7.2 Directional Shadow Map with $4 \times 4$ PCF
* $2048 \times 2048$ 32-bit depth FBO.
* $4 \times 4$ Percentage-Closer Filtering (PCF) for smooth, soft penumbra transitions.

### 7.3 Screen-Space Ambient Occlusion (SSAO)
* Reconstruct view-space positions from depth.
* 16-sample hemisphere kernel rotated by a tiled $4 \times 4$ noise texture.
* Softens crevices, 90-degree wall corners, and the base of SOR pedestals.

### 7.4 Screen-Space Planar Reflection (SSPR) for Puddles
* Floor shader evaluates the procedural puddle mask:
  `float puddle = smoothstep(0.45, 0.55, PuddleNoise(worldPos.xz * 0.3));`
* In puddle areas:
  * Diffuse albedo darkened by $50\%$ (Fresnel absorption);
  * Surface normal clamped to vertical $(0, 1, 0)$;
  * **SSPR Raymarch**: Perform a 16-step 2D screen-space raymarch downwards along the reflected ray in the rendered scene color buffer to reflect the maze walls and torches directly onto the puddle surface!

### 7.5 Parallax Occlusion Mapping (POM) on Maze Walls
* Wall fragment shader performs an 8-to-16 step raymarch in tangent space through the procedural height map.
* Gives flat 2-triangle walls pronounced geometric depth and mortar shadow crevices.

### 7.6 ACES Filmic Tone Mapping & Gamma Correction
Every fragment shader terminates with:
```glsl
vec3 ACESFilm(vec3 x) {
    float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

// Final color output:
color = ACESFilm(color);
FragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
```

---

## 8. Automated Verification Protocol (`--smoke-test`)

Both `SOR_Modeller.exe` and `MazeGame.exe` must support the `--smoke-test` argument:
* Initializes GLFW, creates the OpenGL context;
* Compiles all shaders and synthesizes procedural textures;
* Runs for exactly **120 frames** without user interaction;
* Verifies zero GL error flags (`glGetError() == GL_NO_ERROR`);
* Exits cleanly with code `0`.

### Single Command Build Sequence
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
Produces:
* `build/bin/Release/SOR_Modeller.exe`
* `build/bin/Release/MazeGame.exe`
