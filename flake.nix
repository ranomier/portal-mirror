{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in
    {
      devShells.${system}.default = pkgs.mkShell {
        buildInputs = with pkgs; [
          libportal
          glib
          pkg-config
          gcc
        ];
      };

      packages.${system}.default = pkgs.stdenv.mkDerivation {
        name = "screencast";
        src = ./.;
        nativeBuildInputs = with pkgs; [ pkg-config ];
        buildInputs = with pkgs; [ libportal glib ];
        buildPhase = ''
          gcc -o screencast main.c `pkg-config --cflags --libs libportal`
        '';
        installPhase = ''
          mkdir -p $out/bin
          cp screencast $out/bin/
        '';
      };
    };
}
