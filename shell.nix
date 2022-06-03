{ pkgs ? import <nixpkgs> {}
}:

pkgs.mkShell {
  buildInputs = [
    pkgs.cmake
    pkgs.pkgconfig
    pkgs.libftdi1
  ];
}