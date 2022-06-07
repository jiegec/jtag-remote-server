{ pkgs ? import <nixpkgs> {}
}:

pkgs.mkShell {
  buildInputs = with pkgs; [
    cmake
    pkgconfig
    libftdi1
    gdb
  ];
}
