let

  pkgs = import ./nixpkgs.nix;
  # Flags to debug ccache
  # export CCACHE_DEBUG=1
  # export CCACHE_DEBUGDIR="/nix/var/cache/ccache/log"
  release_version = "1.0.0";
  sharedccacheExtraConfig = ''
        export CCACHE_DIR=/nix/var/cache/ccache
        # Keep cache keys and debug paths stable across randomized Nix build directories.
        export CCACHE_BASEDIR="$NIX_BUILD_TOP"
        # Ignore -frandom-seed: https://github.com/NixOS/nixpkgs/issues/109033
        export CCACHE_SLOPPINESS=random_seed
        export CCACHE_UMASK=007
        if [ ! -d "$CCACHE_DIR" ]; then
          echo "====="
          echo "Directory '$CCACHE_DIR' does not exist"
          echo "Please create it with:"
          echo "  sudo mkdir -m0770 '$CCACHE_DIR'"
          echo "  sudo chown root:nixbld '$CCACHE_DIR'"
          echo "====="
          exit 1
        fi
        if [ ! -w "$CCACHE_DIR" ]; then
          echo "====="
          echo "Directory '$CCACHE_DIR' is not accessible for user $(whoami)"
          echo "Please verify its access permissions"
          echo "====="
          exit 1
        fi
      '';
  ccache-stdenv = pkgs.ccacheStdenv.override {
     extraConfig = sharedccacheExtraConfig;
  };
  clang-stdenv = pkgs.ccacheStdenv.override {
      inherit (pkgs.llvmPackages_latest) stdenv;
      extraConfig = sharedccacheExtraConfig;
    };

  llvm-source-files = fs.difference
    (fs.unions [
      ./llvm-project/clang
      ./llvm-project/cmake
      ./llvm-project/libunwind
      ./llvm-project/lld
      ./llvm-project/lldb
      ./llvm-project/llvm
      ./llvm-project/third-party
      ./llvm-project/nix-build.sh
    ])
    (fs.unions [
      ./llvm-project/clang/test
      ./llvm-project/clang/unittests
      ./llvm-project/lld/test
      ./llvm-project/lld/unittests
      ./llvm-project/lldb/test
      ./llvm-project/lldb/unittests
      ./llvm-project/llvm/test
      ./llvm-project/llvm/unittests
    ]);
  llvm-source = fs.toSource {
    root = ./llvm-project;
    fileset = llvm-source-files;
  };

  bracelet-llvm = clang-stdenv.mkDerivation {
    pname = "bracelet-llvm";
    version = "0.1.0";
    passthru.isClang = true;

    nativeBuildInputs = with pkgs; [
      ninja
      cmake
    ];

    buildInputs = with pkgs; [
      python312
      zlib
    ];

    src = llvm-source;

    configurePhase = ''
      runHook preConfigure
      patchShebangs ./nix-build.sh
      prefixMapFlags="-fdebug-prefix-map=$NIX_BUILD_TOP=/build -ffile-prefix-map=$NIX_BUILD_TOP=/build"
      export CFLAGS="''${CFLAGS:-} $prefixMapFlags"
      export CXXFLAGS="''${CXXFLAGS:-} $prefixMapFlags"
      substituteInPlace ./nix-build.sh \
        --replace-fail \
          "-DLLVM_APPEND_VC_REV=OFF \\" \
          "-DLLVM_APPEND_VC_REV=OFF -DLLVM_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_TESTS=OFF \\"
      ./nix-build.sh RelWithDebInfo $out
      runHook postConfigure
    '';

    # Build only the dependency
    buildPhase = ''
      # Example: build a library or tool
      ninja -C ./build/RelWithDebInfo
    '';

    installPhase = ''
      ninja -C ./build/RelWithDebInfo install
    '';
  };
  bracelet-llvm-wrapped = pkgs.wrapCC(bracelet-llvm);
  fs = pkgs.lib.fileset;
  git-files = fs.gitTracked ./.;
  ci-source-files = fs.intersection git-files (fs.unions [
    ./README.md
    ./build_base
    ./include
    ./meson.build
    ./meson.options
    ./pyproject.toml
    ./src
    ./subprojects
    ./uv.lock
  ]);
  bracelet = clang-stdenv.mkDerivation {
    __noChroot = true;
    pname = "bracelet";
    version = "0.1.0";

    src = fs.toSource {
        # Point to your local source directory
        root = ./.;
        # Define filters to ignore irrelevant files
        fileset = ci-source-files;
      };

    nativeBuildInputs = with pkgs; [
      meson
      ninja
      pkg-config
      uv
      python312
      cacert
    ];

    buildInputs = with pkgs; [
      bracelet-llvm
    ]; 

    configurePhase = ''
      runHook preConfigure
      export UV_PYTHON=${pkgs.python312}
      export UV_VENV_CLEAR=1
      export UV_CACHE_DIR=$TMPDIR/uv-cache
      env LLVM_CONFIG=$llvmconf uv run meson setup builddir --prefix=$out -Dtest=false -Dclang-dir=${bracelet-llvm-wrapped}/bin

      runHook postConfigure
    '';

    buildPhase = ''
      runHook preBuild

      source .venv/bin/activate
      meson compile -C builddir
      runHook postBuild
    '';

    installPhase = ''
      runHook preInstall
      source .venv/bin/activate
      meson install -C builddir
      runHook postInstall
    '';

    doInstallCheck = true;
    installCheckPhase = ''
      runHook preInstallCheck
      printf 'int main(void) { return 0; }\n' > "$TMPDIR/compiler-test.c"
      "$out/bin/bracelet-cc.sh" "$TMPDIR/compiler-test.c" -o "$TMPDIR/compiler-test"
      "$TMPDIR/compiler-test"
      runHook postInstallCheck
    '';

    meta = with pkgs.lib; {
      description = "My Meson project built with uv";
      license = licenses.mit;
      platforms = platforms.unix;
    };
  };
  svf= ccache-stdenv.mkDerivation {
    pname = "svf";
    version = "0.1.0";

    nativeBuildInputs = with pkgs; [
      ninja
      cmake
      cacert
      unzip
      wget
    ];

    buildInputs = with pkgs; [
      zlib
      llvmPackages_16.llvm
      llvmPackages_16.clang
      z3
    ];

    src = ./svf;

    configurePhase = ''
      runHook preConfigure
      cmake -B build -DCMAKE_INSTALL_PREFIX=$out -DLLVM_DIR=${pkgs.llvmPackages_16.llvm} -DZ3_DIR=${pkgs.z3} -DSVF_TOOLS=Bracelet .
      runHook postConfigure
    '';

    # Build only the dependency
    buildPhase = ''
    cmake --build ./build -j $NIX_BUILD_CORES
    '';

    installPhase = ''
    cmake --install ./build
    ls $out
    '';
  };

  
inherit (pkgs) lib;

  pyproject-nix = import (builtins.fetchGit {
    url = "https://github.com/pyproject-nix/pyproject.nix.git";
  }) {
    inherit lib;
  };

  uv2nix = import (builtins.fetchGit {
    url = "https://github.com/pyproject-nix/uv2nix.git";
  }) {
    inherit pyproject-nix lib;
  };

  pyproject-build-systems = import (builtins.fetchGit {
    url = "https://github.com/pyproject-nix/build-system-pkgs.git";
  }) {
    inherit pyproject-nix uv2nix lib;
  };

python = lib.head (pyproject-nix.lib.util.filterPythonInterpreters {
  inherit (workspace) requires-python;
  inherit (pkgs) pythonInterpreters;
});
pythonBase = pkgs.callPackage pyproject-nix.build.packages {
  inherit python;
};
python-source = fs.toSource {
  root = ./.;
  fileset = fs.unions [
    ./README.md
    ./pyproject.toml
    ./src
    ./uv.lock
  ];
};
workspace = uv2nix.lib.workspace.loadWorkspace { workspaceRoot = python-source; };
overlay = workspace.mkPyprojectOverlay {
  sourcePreference = "wheel";
};
  
# Apply the overlay
pythonSet = pythonBase.overrideScope (
  lib.composeManyExtensions [
    pyproject-build-systems.wheel
    overlay
  ]
);
ubuntuImageBase = pkgs.dockerTools.pullImage {
  imageName = "ubuntu";
  imageDigest = "sha256:d1e2e92c075e5ca139d51a140fff46f84315c0fdce203eab2807c7e495eff4f9";
  hash = "sha256-AKl8b+ZMK1SWAfbbrlI1Re4hdp4GS1lUXc+ga2/k4/8=";
  finalImageName = "ubuntu";
  finalImageTag = "24.04";
};

bracelet-scripts = pythonSet.mkVirtualEnv "bracelet-scripts-env" workspace.deps.default;
content-list = with pkgs; [
  dockerTools.caCertificates
  xz
  gdb
  cmake
  ninja
  gnumake
  ccache
  pkg-config-unwrapped
  autoconf
  automake
  libtool
  m4
  gitMinimal
  curl
  zip
  unzip
  bison
  flex
  bracelet
  svf
  vcpkg
  souffle
  llvmPackages_16.llvm
  llvmPackages_16.clang
  stdenv.cc.cc.lib
  bracelet-scripts
];
bin-env = with pkgs; pkgs.buildEnv {
                extraPrefix = "/usr";
                name = "bracelet-env";
                paths = content-list;
              };
docker-image = with pkgs; dockerTools.streamLayeredImage {
  name = "bracelet-toolchain";
  tag = release_version;
  fromImage = ubuntuImageBase;
  contents = [
    bin-env
    dockerTools.caCertificates
  ];
  config.Env = [
    "TZ=America/New_York"
    # buildEnv relocates libtool's macros here
    "ACLOCAL_PATH=/usr/share/aclocal"
    # Programs built in the container need the GCC C++ runtime at execution.
    "LD_LIBRARY_PATH=${stdenv.cc.cc.lib}/lib"
    "SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt"
    "GIT_SSL_CAINFO=/etc/ssl/certs/ca-certificates.crt"
    "VCPKG_OVERLAY_TRIPLETS=${bracelet}/toolchain"
    "BRACELET_TOOLCHAIN_FILE=${bracelet}/toolchain/bracelet-toolchain.cmake"
    "VCPKG_TOOLCHAIN_FILE=${pkgs.vcpkg}/share/vcpkg/scripts/buildsystems/vcpkg.cmake"
    "BRACELET_INCLUDE_DIR=${bracelet}/include"
    "SVF_PATH=${svf}"
    "SVF_CLANG_PATH=${pkgs.llvmPackages_16.clang}"
    "SVF_LLVM_PATH=${pkgs.llvmPackages_16.llvm}"
  ];
  config.Cmd = [ "${pkgs.bash}/bin/bash" ];
};

# FHS User Environment
pipzone-fhs = pkgs.buildFHSUserEnv {
  name = "pipzone";
  targetPkgs = p: (with p; [
    python312Full
    python312Packages.pip
    python312Packages.virtualenv
  ]);
  multiPkgs = p: with p; [
    libgcc
    binutils
    coreutils
  ];
  profile = ''
    export LIBRARY_PATH=/usr/lib:/usr/lib64:$LIBRARY_PATH
  '';
  runScript = "bash";
};

in
{
  bracelet-llvm = bracelet-llvm;
  bracelet = bracelet;
  svf = svf;
  docker-image = docker-image;
  bracelet-scripts = bracelet-scripts;
  ub-image = ubuntuImageBase;
  pipzone-shell = pipzone-fhs.env; # Exposing the FHS shell here
}
