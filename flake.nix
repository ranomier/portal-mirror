{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      buildInputs = with pkgs; [
        libportal
        glib
        pipewire
        sdl3
      ];
    in
    {
      devShells.${system}.default = pkgs.mkShell {
        buildInputs = buildInputs ++ (with pkgs; [ pkg-config gcc ]);
      };

      packages.${system}.default = pkgs.stdenv.mkDerivation {
        name = "screencast";
        src = ./.;
        nativeBuildInputs = with pkgs; [ pkg-config ];
        buildInputs = buildInputs;
        buildPhase = ''
          gcc -o screencast main.c `pkg-config --cflags --libs libportal gio-2.0 pipewire sdl3`
        '';
        installPhase = ''
          mkdir -p $out/bin
          cp screencast $out/bin/
        '';
      };
    };
}
