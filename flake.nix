{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    devkitNix.url = "github:bandithedoge/devkitNix";
  };

  outputs = { self, nixpkgs, devkitNix, hax-nur }: {
    packages.x86_64-linux = let
      pkgs = import nixpkgs { system = "x86_64-linux"; overlays = [ devkitNix.overlays.default ]; };
    in rec {
      restore-overwritten-secureinfo = pkgs.stdenvNoCC.mkDerivation rec {
        pname = "restore-overwritten-secureinfo";
        version = "unstable";
        src = builtins.path { path = ./.; name = "restore-overwritten-secureinfo"; };

        preBuild = pkgs.devkitNix.devkitARM.shellHook;

        makeFlags = [ "TARGET=restore-overwritten-secureinfo" ];

        installPhase = ''
          cp restore-overwritten-secureinfo.3dsx $out
        '';
      };
      default = restore-overwritten-secureinfo;
    };
  };
}
