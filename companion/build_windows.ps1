$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$python = Join-Path $repoRoot ".venv\Scripts\python.exe"
$requirements = Join-Path $PSScriptRoot "requirements-build.txt"
$entryPoint = Join-Path $PSScriptRoot "launcher.py"
$distPath = Join-Path $repoRoot "dist"
$buildPath = Join-Path $repoRoot ".build\pyinstaller"

# Kill any existing StickS3Companion processes
Write-Host "[Companion] Checking for existing companion processes..."
$processes = Get-Process -Name "StickS3Companion" -ErrorAction SilentlyContinue
if ($processes) {
    Write-Host "[Companion] Killing existing StickS3Companion process(es)..."
    $processes | Stop-Process -Force
    Write-Host "[Companion] Existing processes killed."
} else {
    Write-Host "[Companion] No existing companion processes found."
}

# Also kill any Python processes running the companion
$pythonProcs = Get-WmiObject Win32_Process -Filter "Name = 'python.exe' AND (CommandLine LIKE '%launcher.py%' OR CommandLine LIKE '%live_transcriber.py%')" -ErrorAction SilentlyContinue
if ($pythonProcs) {
    Write-Host "[Companion] Killing existing companion Python processes..."
    $pythonProcs | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
    Write-Host "[Companion] Existing Python processes killed."
}

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
