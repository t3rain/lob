$a = @(); $b = @()
for ($i = 0; $i -lt 6; $i++) {
    foreach ($r in @(0, 1048576)) {
        $out = & .\bench_rel.exe 1 5000000 10 $r
        $m = [double]($out | Select-String 'THROUGHPUT\s+median\s+([\d\.]+)').Matches[0].Groups[1].Value
        if ($r -eq 0) { $a += $m } else { $b += $m }
    }
}
"no reserve : " + ($a -join ", ")
"1M reserve : " + ($b -join ", ")