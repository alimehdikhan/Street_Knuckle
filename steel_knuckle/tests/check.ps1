param([switch]$BuildGame)
$ErrorActionPreference='Continue'
$projectRoot=Split-Path (Split-Path $PSScriptRoot)
Set-Location $projectRoot
$cache=Get-Content build/game/CMakeCache.txt
$compiler=($cache | Where-Object {$_ -match '^CMAKE_CXX_COMPILER:FILEPATH='}) -replace '^CMAKE_CXX_COMPILER:FILEPATH=',''
$cmake=($cache | Where-Object {$_ -match '^CMAKE_COMMAND:INTERNAL='}) -replace '^CMAKE_COMMAND:INTERNAL=',''
$env:PATH=(Split-Path $compiler)+';'+$env:PATH
& $compiler -std=c++17 -O0 -DPLATFORM_WIN32=1 '@build/game/CMakeFiles/steel_knuckle.dir/includes_CXX.rsp' steel_knuckle/tests/combat_tests.cpp steel_knuckle/src/game.cpp -o build/combat_tests.exe
if($LASTEXITCODE -ne 0){throw 'Combat tests did not compile'}
& ./build/combat_tests.exe
if($LASTEXITCODE -ne 0){throw 'Combat tests failed'}
if($BuildGame){
    & $cmake --build build/game --target steel_knuckle -j 4
    if($LASTEXITCODE -ne 0){throw 'Game build failed'}
}
