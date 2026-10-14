param(
    [string]$Target = "all",
    [ValidateSet("debug", "check", "release")][string]$Mode = "debug"
)
Set-Location $PSScriptRoot

$common = @("-std=c++17", "-Wall", "-Wextra", "-Iinc")
switch ($Mode) {
    "debug"   { $flags = $common + @("-g") }
    "check"   { $flags = $common + @("-g", "-O1", "-D_GLIBCXX_DEBUG", "-D_GLIBCXX_ASSERTIONS") }
    "release" { $flags = $common + @("-O2", "-DNDEBUG") }
}
$suffix = @{ debug = "_dbg"; check = "_chk"; release = "_rel" }[$Mode]

# name of the executable -> the file that contains its main()
# EDIT THESE to match your real file names
$targets = [ordered]@{
    test_basic     = "tests/test_basic.cpp"
    test_reference = "tests/test_reference.cpp"
    fuzz           = "tests/fuzz.cpp"
    bench          = "bench/bench.cpp"
}

if ($Target -ne "all" -and -not $targets.Contains($Target)) {
    Write-Host "Unknown target '$Target'. Choices: $($targets.Keys -join ', '), all"
    exit 1
}

foreach ($name in $targets.Keys) {
    if ($Target -ne "all" -and $Target -ne $name) { continue }
    $src = $targets[$name]
    if (-not (Test-Path $src)) { Write-Host "skip $name (no $src)"; continue }

    $out = "$name$suffix.exe"
    Write-Host "building $out  [$Mode]"
    g++ @flags src/order_book.cpp $src -o $out
    if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED: $name"; exit 1 }
}
Write-Host "done"