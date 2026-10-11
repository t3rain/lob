# run_fuzz.ps1
param(
    [int]$Seeds = 1000,
    [int]$Steps = 5000,
    [int]$Width = 3        # half-width of the price range: 3 -> 97..103, 1 -> 99..101
)

for ($s = 1; $s -le $Seeds; $s++) {
    .\fuzz.exe $s $Steps $Width > out.txt
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED at seed $s (steps=$Steps, width=$Width)"
        Get-Content out.txt
        exit 1
    }
}
Write-Host "all $Seeds seeds passed (steps=$Steps, width=$Width)"