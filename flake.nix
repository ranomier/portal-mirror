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
          pipewire
          SDL2
          pkg-config
          gcc
        ];
      };

      packages.${system}.default = pkgs.stdenv.mkDerivation {
        name = "screencast";
        src = ./.;
        nativeBuildInputs = with pkgs; [ pkg-config ];
        buildInputs = with pkgs; [ libportal glib pipewire SDL2 ];
        buildPhase = ''
          gcc -o screencast main.c `pkg-config --cflags --libs libportal pipewire-0.3 sdl2`
        '';
        installPhase = ''
          mkdir -p $out/bin
          cp screencast $out/bin/
        '';
      };
    };
}
