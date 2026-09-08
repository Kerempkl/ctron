{
  description = "Ctron — ASUS TUF Gaming Control Applet in C and Notcurses (Arcioth & Kerempkl)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/a3b98866eecd";
  };

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
    in {
      packages.${system} = {
        default = pkgs.stdenv.mkDerivation {
          pname = "ctron";
          version = "1.0.0";
          src = ./.;

          nativeBuildInputs = [ pkgs.pkg-config ];
          buildInputs = [ pkgs.notcurses ];

          buildPhase = ''
            make
          '';

          installPhase = ''
            mkdir -p $out/bin $out/share/applications
            cp build/ctron $out/bin/ctron
            ln -s $out/bin/ctron $out/bin/vhelper
            ln -s $out/bin/ctron $out/bin/tufhelper
            cp ctron.desktop $out/share/applications/
          '';
        };
        ctron = self.packages.${system}.default;
      };

      apps.${system} = {
        default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/ctron";
        };
      };

      devShells.${system}.default = pkgs.mkShell {
        buildInputs = [
          pkgs.gcc
          pkgs.pkg-config
          pkgs.notcurses
          pkgs.asusctl
        ];
      };
    };
}
