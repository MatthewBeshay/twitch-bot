param(
  [string]$Config = ".cmake-format.yaml",
  [string[]]$Include = @('CMakeLists.txt','*.cmake'),
  [string[]]$SkipDirs = @('\external\','\out\','\build\','\vcpkg\','\CMakeFiles\','\_deps\'),
  [string[]]$SkipFiles = @('cmake_install.cmake','CMakeSystem.cmake','CMakeCXXCompiler.cmake','CMakeRCCompiler.cmake')
)

$files = Get-ChildItem -Recurse -File -Include $Include | Where-Object {
  $p = $_.FullName
  (-not ($SkipDirs  | ForEach-Object { $p -like "*$_*" })) -and
  (-not ($SkipFiles | ForEach-Object { $p -like "*\$_"   }))
}

foreach($f in $files){
  Write-Host "Formatting $($f.FullName)"
  & cmake-format -c $Config -i $f.FullName
}
