# Build the TRUEAE startup fix for Skyrim 1.6.1170

This snapshot contains the patched Seasons of Skyrim v1.9.1 source based on
commit 89837a27849c97a9f91d01f2911d14b49bef2662. The CommonLibSSE submodule points to the 1.6.1170 development branch at
commit 332bcf6a2f89f8cf781df864e55aebac22db8268.

Open PowerShell in the extracted project. Install Visual Studio 2022 Build Tools
with the x64 v143 toolset and a Windows SDK, and make Git and CMake available.

    git submodule update --init extern/CommonLibSSE

    git init build-vcpkg
    git -C build-vcpkg fetch --depth 1 https://github.com/microsoft/vcpkg.git 00c5775211f45cd08b37fce0484b4cb940e422ab
    git -C build-vcpkg switch --detach FETCH_HEAD
    & .\build-vcpkg\bootstrap-vcpkg.bat -disableMetrics

    $seasonsVcpkg = (Resolve-Path .\build-vcpkg).Path
    $env:CommonLibSSEPath = (Resolve-Path .\extern\CommonLibSSE).Path
    cmake -S . -B build-ae -G "Visual Studio 17 2022" -A x64 -T v143 -DBUILD_SKYRIMAE=ON -DCOPY_BUILD=OFF "-DVCPKG_ROOT=$seasonsVcpkg" -DCMAKE_CONFIGURATION_TYPES=Release
    cmake --build build-ae --config Release --target po3_SeasonsOfSkyrim

The DLL and PDB are produced in `build-ae/Release`. This source snapshot
targets the Skyrim 1.6.1170 build used to test the fix.
