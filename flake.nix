{
  description = "ctron — lightweight ASUS laptop control center (fullscreen TUI + headless CLI, notcurses only)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll = nixpkgs.lib.genAttrs systems;
    in {
      packages = forAll (system:
        let pkgs = nixpkgs.legacyPackages.${system};
        in {
          ctron = pkgs.stdenv.mkDerivation {
            pname = "ctron";
            # keep in sync with src/main.c VERSION — bumps ride the
            # release commit (see NEXT.md "Sürüm politikası")
            version = "0.9.0";
            src = self;
            nativeBuildInputs = [ pkgs.pkg-config ];
            buildInputs = [ pkgs.notcurses ];
            # plain Makefile (no configure); it honors stdenv CFLAGS
            installPhase = ''
              runHook preInstall
              install -Dm755 build/ctron -t $out/bin
              runHook postInstall
            '';
          };
          default = self.packages.${system}.ctron;
        });

      # the documented `nix-shell -p gcc pkg-config notcurses gnumake`
      # workflow, made permanent: `nix develop` then `make`
      devShells = forAll (system:
        let pkgs = nixpkgs.legacyPackages.${system};
        in {
          default = pkgs.mkShell {
            packages = with pkgs; [ gcc pkg-config notcurses gnumake ];
          };
        });
    };
}
