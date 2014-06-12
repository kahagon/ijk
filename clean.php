#!/usr/bin/env hhvm
<?hh
$cmakeFilesToBeRemoved = [
  'CMakeLists.txt',
  'CMakeFiles',
  'CMakeCache.txt',
  'Makefile',
  'cmake_install.cmake',
];

$cmd = "make clean 2>&1";
print "$cmd\n";
system($cmd, $result);
if ($result != 0) {
    exit($result);
}

$files = '';
foreach ($cmakeFilesToBeRemoved as $file) {
  $files .= " ${file}";
}
if ($files) {
  $cmd = "rm -rf " . $files;
  print "$cmd\n";
  system($cmd, $result);
  if ($result != 0) {
      exit($result);
  }
}
