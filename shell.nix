{ pkgs ? import <nixpkgs> {}
}:

pkgs.mkShell {
  buildInputs = with pkgs; [
    cmake
    pkg-config
    libftdi1
    gdb
  ];
}
