$ErrorActionPreference = "Stop"

$imageName = "poc-hbdt-seal"
$containerBuildDir = "/workspace/build-podman"
$sealBuildJobs = 2

podman build --build-arg SEAL_BUILD_JOBS=$sealBuildJobs -t $imageName -f Containerfile .

podman run --rm `
  -v "${PWD}:/workspace" `
  -w /workspace `
  $imageName `
  bash -lc "cmake -S . -B $containerBuildDir -DCMAKE_BUILD_TYPE=Release -DSEAL_ROOT=`$SEAL_ROOT -DSEAL_DIR=`$SEAL_DIR && cmake --build $containerBuildDir --parallel"
