# bipbip thirdparty build notes

## Toolchain
- Compiler: C:/msys64/mingw64 (GCC 16.2). MUST have `/c/msys64/mingw64/bin` on PATH or cc1.exe silently fails to load its DLLs.
- CMake: portable at `bipbip/tools/cmake-3.30.5-windows-x86_64/bin/cmake.exe` (pacman is broken on this host).
- Ninja: `bipbip/tools/ninja.exe`.
- Native tools get `C:/...` paths only; MSYS `/c/...` paths fail inside them.

## Protobuf (GNS dependency) — manual install (pacman DB corrupted)
```bash
cd /tmp   # git-bash /tmp == C:/Users/apex/AppData/Local/Temp
curl -sL -O https://repo.msys2.org/mingw/mingw64/mingw-w64-x86_64-protobuf-35.1-1-any.pkg.tar.zst
curl -sL -O https://repo.msys2.org/mingw/mingw64/mingw-w64-x86_64-abseil-cpp-20260526.0-1-any.pkg.tar.zst
zstd -d -f <pkg>.pkg.tar.zst -o <pkg>.pkg.tar
cd /c/msys64 && bsdtar.exe -xf "C:/Users/apex/AppData/Local/Temp/<pkg>.pkg.tar"
```
Pitfall: bsdtar is native — pass Windows paths; cygpath /tmp lies (maps to msys64/tmp).

## Physics: custom verlet solver (engine/physics/verlet.*)
Jolt was abandoned after integration hell: lib built Release vs Debug TUs caused
`JPH_ENABLE_ASSERTS`/`JPH_PROFILE_ENABLED`/`JPH_DEBUG_RENDERER`/`JPH_OBJECT_STREAM`
define mismatches (runtime version check aborts). Own solver instead:
particles + distance constraints + heightfield/AABB collision, fixed 60Hz,
deterministic (network-friendly). Ragdoll climber: semi-kinematic pelvis +
verlet limbs on constraints (game/player/climber.*).
If ever resurrecting Jolt: build with `-DUSE_ASSERTS=ON`, match ALL defines
(JPH_ENABLE_ASSERTS JPH_OBJECT_STREAM [+ renderer/profiler if on]) in consumer
target, AND build consumer RelWithDebInfo (-DNDEBUG) not Debug.
## GameNetworkingSockets 1.6.0
Source: C:/Users/apex/Downloads/GameNetworkingSockets-1.6.0/GameNetworkingSockets-1.6.0
Build dir: thirdparty/gns-build
```
cmake -G Ninja \
  -DCMAKE_MAKE_PROGRAM=C:/Users/apex/bipbip/tools/ninja.exe \
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe \
  -DCMAKE_PREFIX_PATH=C:/msys64/mingw64 \
  -DCMAKE_BUILD_TYPE=Release \
  -DUSE_CRYPTO=BCrypt -DBUILD_STATIC_LIB=ON -DBUILD_SHARED_LIB=OFF \
  -DBUILD_TESTS=OFF -DBUILD_TOOLS=OFF <src>
cmake --build .
```
Result: libGameNetworkingSockets_s.a (+ protobuf/abseil static deps from mingw64).
Crypto: BCrypt (native Windows); ed25519/curve25519 = built-in reference impl (fine for our use).

## Jolt Physics v5.1.0
Source: thirdparty/JoltPhysics (git clone --depth 1 --branch v5.1.0)
Build dir: thirdparty/jolt-build
SSE2-only config for Intel Gemini Lake host CPU:
```
-DUSE_SSE4=OFF -DUSE_AVX2=OFF -DUSE_FMA=OFF -DUSE_AVX512=OFF
-DUSE_LZCNT=OFF -DUSE_BMI1=OFF -DUSE_TZCNT=OFF
-DTARGET_HELLO_WORLD=OFF -DTARGET_UNIT_TESTS=OFF -DTARGET_PERFORMANCE_TEST=OFF
-DTARGET_SAMPLES=OFF -DTARGET_VIEWER=OFF -DINTERPROCEDURAL_OPTIMIZATION=OFF
```
Result: libJolt.a

## Link order for game exe (static archives)
bipbip_engine -> Jolt -> GameNetworkingSockets_s -> protobuf/abseil -> system (ws2_32 bcrypt crypt32 winmm d3d11 dxgi d3dcompiler)
