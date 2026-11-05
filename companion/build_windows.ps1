$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$python = Join-Path $repoRoot ".venv\Scripts\python.exe"
$requirements = Join-Path $PSScriptRoot "requirements-build.txt"
$entryPoint = Join-Path $PSScriptRoot "launcher.py"
$distPath = Join-Path $repoRoot "dist"
$buildPath = Join-Path $repoRoot ".build\pyinstaller"

if (-not (Test-Path $python)) {
    throw "Project virtual environment not found at $python. Create .venv and install companion requirements first."
}

& $python -m pip uninstall -y onnxruntime
if ($LASTEXITCODE -ne 0) {
    throw "Failed to remove the CPU-only ONNX Runtime package before installing the CUDA build."
}

& $python -m pip install -r $requirements
if ($LASTEXITCODE -ne 0) {
    throw "Failed to install Windows executable build dependencies."
}

& $python -m PyInstaller `
    --noconfirm `
    --clean `
    --onedir `
    --windowed `
    --name StickS3Companion `
    --distpath $distPath `
    --workpath $buildPath `
    --specpath $buildPath `
    --paths $PSScriptRoot `
    --collect-submodules bleak.backends.winrt `
    --collect-all onnx_asr `
    --collect-all onnxruntime `
    --collect-binaries nvidia `
    --collect-all huggingface_hub `
    --collect-all winrt `
    $entryPoint
if ($LASTEXITCODE -ne 0) {
    throw "PyInstaller failed to build StickS3Companion."
}

$executable = Join-Path $distPath "StickS3Companion\StickS3Companion.exe"
if (-not (Test-Path $executable)) {
    throw "Build finished without the expected executable: $executable"
}

Write-Host "Built $executable"
