let
  pkgs = import ./nixpkgs.nix;
  my-pkgs = import ./default.nix;
  
  # Grouping the inputs makes it easy to pass them to both buildInputs and makeLibraryPath
  shellInputs = [
    pkgs.xz 
    my-pkgs.bracelet
    my-pkgs.svf 
    pkgs.uv 
    pkgs.python312 
    pkgs.cmake 
    pkgs.pkg-config-unwrapped 
    pkgs.vcpkg 
    pkgs.souffle 
    pkgs.llvmPackages_16.llvm 
    pkgs.llvmPackages_16.clang
    pkgs.autoconf
    pkgs.libtool
    
     pkgs.stdenv.cc.cc.lib
     pkgs.zlib
  ];
in

pkgs.mkShellNoCC {
  # Add the build inputs of the package to the shell's environment
  buildInputs = shellInputs;
  
  shellHook = ''
    export VCPKG_OVERLAY_TRIPLETS="${my-pkgs.bracelet}/toolchain";
    export BRACELET_TOOLCHAIN_FILE="${my-pkgs.bracelet}/toolchain/bracelet-toolchain.cmake"
    export VCPKG_TOOLCHAIN_FILE="${pkgs.vcpkg}/share/vcpkg/scripts/buildsystems/vcpkg.cmake"
    export BRACELET_INCLUDE_DIR="${my-pkgs.bracelet}/include"
    export SVF_PATH="${my-pkgs.svf}"
    export SVF_CLANG_PATH="${pkgs.llvmPackages_16.clang}"
    export SVF_LLVM_PATH="${pkgs.llvmPackages_16.llvm}"
    
    # Automatically generate the LD_LIBRARY_PATH from the shellInputs
    export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath shellInputs}:$LD_LIBRARY_PATH"
    
    uv venv 
    uv sync 
    source .venv/bin/activate
  '';
}
