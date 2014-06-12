include("LLVM.cmake")

HHVM_EXTENSION(ijk ijk.cpp translator.cpp)
HHVM_SYSTEMLIB(ijk ext_ijk.php)

target_link_libraries(ijk ${LLVM_LIBS})
