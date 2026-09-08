$ErrorActionPreference = 'Stop'
$compiler = (Get-Command g++ -ErrorAction Stop).Source
New-Item -ItemType Directory -Force build | Out-Null
$flags = @('-std=c++17', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Iinclude')

& $compiler @flags src/engine.cpp src/replication.cpp src/main.cpp -o build/minikv.exe
if ($LASTEXITCODE -ne 0) { throw 'CLI build failed' }
& $compiler @flags src/engine.cpp src/replication.cpp tests/test_engine.cpp -o build/minikv_tests.exe
if ($LASTEXITCODE -ne 0) { throw 'test build failed' }
& $compiler @flags src/engine.cpp src/replication.cpp benchmarks/benchmark.cpp -o build/minikv_bench.exe
if ($LASTEXITCODE -ne 0) { throw 'benchmark build failed' }

Write-Host 'Built build/minikv.exe, build/minikv_tests.exe, build/minikv_bench.exe'

