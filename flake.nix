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

      packages.${system}.default = let
        name = "portal-mirror";
      in pkgs.stdenv.mkDerivation {
        name = name;
        src = ./.;
        nativeBuildInputs = with pkgs; [ pkg-config ];
        buildInputs = buildInputs;
        buildPhase = ''
          gcc -o ${name} main.c $(pkg-config --cflags --libs libportal gio-2.0 libpipewire-0.3 sdl3)
        '';
        installPhase = ''
          mkdir -p $out/bin
          cp ${name} $out/bin/
        '';
      };
    };
}
