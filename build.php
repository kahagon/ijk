#!/usr/bin/env hhvm
<?hh
$options = getopt('', ['llvm-home-dir:']);

$cmd = "hphpize 2>&1";
print "$cmd\n";
system($cmd, $result);
if ($result != 0) {
    exit($result);
}

$llvmHomeDir = '';
if (is_array($options) && array_key_exists('llvm-home-dir', $options)) {
    $dir = realpath($options['llvm-home-dir']);
    if (!$dir) {
        print "invalid llvm-home-dir\n";
        exit(1);
    }
    $llvmHomeDir = 'CMAKE_PREFIX_PATH=' . $dir;
}

$cmd = "$llvmHomeDir cmake . 2>&1";
print "$cmd\n";
system($cmd, $result);
if ($result != 0) {
    exit($result);
}

$cmd = "make";
print "$cmd\n";
system($cmd, $result);
if ($result != 0) {
    exit($result);
}
